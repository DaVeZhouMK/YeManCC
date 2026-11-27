#pragma once
// Export-time file/staging evidence only; no hardware, controllers or polling.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <memory>
#include <utility>
#include <algorithm>
#include <array>
#include <system_error>
#include <new>
#include "json.hpp"
#include "buffered_domain_log.h"
namespace ymcc::logexport {
using EvidenceJson=nlohmann::json;
struct ArchiveChildFailure {
    const char* completion=nullptr;
    bool unicodeZip=false,ran=false,captureFinalizationEntered=false;
    DWORD exitCode=ERROR_SUCCESS,win32Error=ERROR_SUCCESS;
    std::size_t retainedOutputBytes=0;

    template<class Run>
    void observe(bool unicode,const Run& run) noexcept {
        if(run.ran&&run.exitCode==ERROR_SUCCESS)return;
        completion=run.completion;unicodeZip=unicode;ran=run.ran;
        exitCode=run.exitCode;win32Error=run.win32Error;
        captureFinalizationEntered=run.captureFinalization.entered;
        retainedOutputBytes=run.out.size();
    }

    void write(EvidenceJson& attempt) const {
        if(!completion)return;
        auto& out=attempt["archiveChildFailure"];out=EvidenceJson::object();
        out["schema"]=1;out["backend"]=unicodeZip?"unicode-zip":"tar";
        out["completion"]=completion;out["ran"]=ran;out["captureFinalizationEntered"]=captureFinalizationEntered;
        out["wrapperExitCode"]=ran?EvidenceJson(exitCode):EvidenceJson(nullptr);
        out["win32Error"]=win32Error;
        out["captureChannel"]=unicodeZip?"stdout-and-stderr":"stderr";
        out["retainedOutputBytes"]=captureFinalizationEntered?EvidenceJson(retainedOutputBytes):EvidenceJson(nullptr);
    }
};

struct FailedArchiveTemporaryObservation {
    bool observed=false;
    const char* state=nullptr;
    DWORD win32Error=ERROR_SUCCESS;
    std::uint64_t bytes=0;
    ULONGLONG elapsedMs=0;

    void afterCleanup(const wchar_t* path) noexcept {
        const auto previousError=GetLastError();
        WIN32_FILE_ATTRIBUTE_DATA data{};
        const auto started=GetTickCount64();
        const bool found=GetFileAttributesExW(path,GetFileExInfoStandard,&data)!=FALSE;
        win32Error=found?ERROR_SUCCESS:GetLastError();
        elapsedMs=GetTickCount64()-started;
        SetLastError(previousError);
        observed=true;
        if(!found){
            state=win32Error==ERROR_FILE_NOT_FOUND||win32Error==ERROR_PATH_NOT_FOUND?"not-found-at-lookup":"attribute-query-failed";
        }else if(data.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DEVICE)){
            state="non-regular-path";
        }else{
            state="regular-file-metadata";
            bytes=(static_cast<std::uint64_t>(data.nFileSizeHigh)<<32)|data.nFileSizeLow;
        }
    }

    EvidenceJson evidence() const {
        return {{"schema",1},{"scope","owned-attempt-temporary-after-failed-cleanup;metadata-only"},
            {"state",state},{"win32Error",win32Error},{"elapsedMs",elapsedMs},
            {"bytes",state==std::string_view("regular-file-metadata")?EvidenceJson(bytes):EvidenceJson(nullptr)}};
    }
};

// Keep only the small terminal projection; full attempt and source details stay in the original log.
inline void addPriorFinalOutputCheck(EvidenceJson& receipt,const EvidenceJson& row) noexcept {
    try {
        const auto count=row.at("attempts");
        if(!count.is_number_integer()||(count!=1&&count!=2))return;
        const auto& timing=row.at("archiveAttemptTiming");
        const auto& attempts=timing.at("attempts");
        if(timing.at("schemaVersion")!=1||!attempts.is_array()||attempts.size()!=count.get<unsigned>()||
            !timing.at("attemptsObserved").is_number_integer()||timing.at("attemptsObserved")!=count)return;
        for(std::size_t i=0;i<attempts.size();++i){
            if(!attempts[i].at("attempt").is_number_integer()||attempts[i].at("attempt")!=i+1)return;
        }
        const auto& last=attempts.back();
        const auto& promotion=last.at("promotionEvidence");
        const auto& move=promotion.at("promotion");
        const auto& validation=promotion.at("postPromotionValidation");
        if(promotion.at("schema")!=1||last.at("attemptBodyReturned")!=true||move.at("observed")!=true||
            move.at("accepted")!=true||move.at("win32Error")!=0||
            validation.at("entered")!=true||validation.at("returned")!=true)return;
        const auto& readable=validation.at("readableAndNonempty");
        if(!readable.is_boolean()||readable!=row.at("ok"))return;
        const auto& checks=last.at("outputCheckEvidence");
        if(checks.at("schema")!=1)return;
        const auto& final=checks.at("final");
        const auto& state=final.at("state");
        const auto& size=final.at("sizeBytes");
        const auto hasOpenError=final.contains("openWin32Error");
        if(state=="stream-opened-nonempty"||state=="stream-open-failed") {
            if(readable!=(state=="stream-opened-nonempty")||!size.is_number_unsigned()||size==0)return;
        }else if(state=="empty-file") {
            if(readable!=false||!size.is_number_unsigned()||size!=0)return;
        }else if(state=="file-status-error"||state=="file-size-error"||state=="not-regular-file") {
            if(readable!=false||!size.is_null())return;
        }else return;
        EvidenceJson output=EvidenceJson::object();
        output["schema"]=1;output["attempt"]=count;
        output["state"]=state;output["sizeBytes"]=size;
        if(hasOpenError){
            const auto& error=final.at("openWin32Error");
            if(state!="stream-open-failed"||!error.is_number_unsigned()||error==0||error>0xffffffffu)return;
            output["openWin32Error"]=error;
        }
        receipt["finalOutputAfterMove"].swap(output);
    }catch(...){receipt.erase("finalOutputAfterMove");}
}

inline void addPriorArchiveSteps(EvidenceJson& receipt,const EvidenceJson& row) noexcept {
    try {
        const auto& count=row.at("attempts");
        const auto& elapsed=row.at("archiveElapsedMs");
        const auto& timing=row.at("archiveAttemptTiming");
        const auto& attempts=timing.at("attempts");
        if(!count.is_number_integer()||(count!=1&&count!=2)||!elapsed.is_number_unsigned()||
            timing.at("schemaVersion")!=1||timing.at("scope")!="current-archive-call-completed-step-wall-ms"||
            !timing.at("attemptsObserved").is_number_integer()||timing.at("attemptsObserved")!=count||
            !attempts.is_array()||attempts.size()!=count.get<unsigned>())return;
        static constexpr std::string_view phases[]={"stagingPreparation","sourceSnapshot","snapshotEvidenceAndIndex",
            "commandPreparation","tarProcess","archiveValidationAndPromotion","attemptCleanup","retryDelay"};
        auto remaining=elapsed.get<std::uint64_t>();
        EvidenceJson steps=EvidenceJson::array();
        for(std::size_t i=0;i<attempts.size();++i){
            const auto& attempt=attempts[i];
            const auto& total=attempt.at("totalElapsedMs");
            const auto& completed=attempt.at("completedStepElapsedMs");
            if(!attempt.at("attempt").is_number_integer()||attempt.at("attempt")!=i+1||
                attempt.at("attemptBodyReturned")!=true||!total.is_number_unsigned()||!completed.is_object()||
                (i==1&&completed.contains("retryDelay")))return;
            auto stepBudget=total.get<std::uint64_t>();
            if(stepBudget>remaining)return;
            remaining-=stepBudget;
            for(auto it=completed.begin();it!=completed.end();++it){
                if(std::find(std::begin(phases),std::end(phases),it.key())==std::end(phases)||
                    !it->is_number_unsigned()||it->get<std::uint64_t>()>stepBudget)return;
                stepBudget-=it->get<std::uint64_t>();
            }
            steps.push_back(EvidenceJson::object());
            auto& step=steps.back();
            step["attempt"]=i+1;step["totalElapsedMs"]=total;step["completedStepElapsedMs"]=completed;
        }
        EvidenceJson result=EvidenceJson::object();
        result["schema"]=1;result["attempts"]=std::move(steps);
        receipt["archiveAttemptSteps"].swap(result);
    }catch(...){receipt.erase("archiveAttemptSteps");}
}

inline void addPriorArchiveChildFailures(EvidenceJson& receipt,const EvidenceJson& row) noexcept {
    try {
        const auto steps=receipt.find("archiveAttemptSteps");
        if(steps==receipt.end())return;
        const auto& attempts=row.at("archiveAttemptTiming").at("attempts");
        const auto& ok=row.at("ok");
        if(!ok.is_boolean())return;
        EvidenceJson failures;
        for(std::size_t i=0;i<attempts.size()-static_cast<unsigned>(ok==true);++i){
            const auto& attempt=attempts[i];
            const auto value=attempt.find("archiveChildFailure");
            if(value==attempt.end())continue;
            try {
                const auto& completed=attempt.at("completedStepElapsedMs");
                if(!completed.contains("commandPreparation")||!completed.contains("tarProcess")||
                    !value->is_object()||value->size()!=9||value->at("schema")!=1)continue;
                const auto& request=attempt.at("zipCompressionRequested");
                const auto& backend=value->at("backend");
                const auto& channel=value->at("captureChannel");
                if(!((backend=="tar"&&channel=="stderr"&&(request=="level-4"||request=="level-5"||request=="tar-default"))||
                    (backend=="unicode-zip"&&channel=="stdout-and-stderr"&&request=="dotnet-optimal")))continue;
                const auto& ran=value->at("ran");
                const auto& entered=value->at("captureFinalizationEntered");
                const auto& code=value->at("wrapperExitCode");
                const auto& error=value->at("win32Error");
                const auto& bytes=value->at("retainedOutputBytes");
                const auto& end=value->at("completion");
                constexpr auto maxBytes=8u<<20;
                const auto bounded=[](const EvidenceJson& n,std::uint64_t limit){return n.is_number_unsigned()&&n<=limit;};
                if(!ran.is_boolean()||!entered.is_boolean()||!bounded(error,0xffffffffu)||
                    (entered==true?!bounded(bytes,maxBytes):!bytes.is_null()))continue;
                if(ran==true){
                    if(entered!=true||!bounded(code,0xffffffffu)||code==0||
                        !((end=="child-exit"&&error==0)||(end=="deadline"&&code==ERROR_TIMEOUT&&error==0)||
                          (end=="wait-failed"&&code==ERROR_TIMEOUT)||
                          (end=="overflow"&&code==ERROR_BUFFER_OVERFLOW&&error==0&&bytes==maxBytes)||
                          (end=="exit-code-failed"&&(error==0?code==ERROR_GEN_FAILURE:code==error))))continue;
                }else if(!code.is_null()||(end=="setup-resume-thread")!=(entered==true)||
                    !(end=="setup-create-pipe"||end=="setup-create-job"||end=="setup-configure-job"||
                      end=="setup-create-process"||end=="setup-assign-job"||end=="setup-resume-thread"))continue;
                failures.push_back({{"attempt",i+1},{"zipCompressionRequested",request},{"archiveChildFailure",*value}});
            }catch(...){ }
        }
        if(!failures.empty())(*steps)["childFailures"].swap(failures);
    }catch(...){ }
}

inline void addPriorArchiveTemporaryLookups(EvidenceJson& receipt,const EvidenceJson& row) noexcept {
    try {
        const auto steps=receipt.find("archiveAttemptSteps");
        if(steps==receipt.end()||row.at("cleanupOk")!=false)return;
        const auto& ok=row.at("ok");
        if(!ok.is_boolean())return;
        const auto& attempts=row.at("archiveAttemptTiming").at("attempts");
        EvidenceJson lookups=EvidenceJson::array();
        for(std::size_t i=0;i<attempts.size()-static_cast<unsigned>(ok==true);++i){
            const auto& attempt=attempts[i];
            const auto value=attempt.find("failedTemporaryAfterCleanup");
            if(value==attempt.end())continue;
            try {
                const auto& duration=attempt.at("completedStepElapsedMs").at("attemptCleanup");
                if(!value->is_object()||value->size()!=6||!value->at("schema").is_number_integer()||
                    value->at("schema")!=1||value->at("scope")!="owned-attempt-temporary-after-failed-cleanup;metadata-only")continue;
                const auto& state=value->at("state");const auto& bytes=value->at("bytes");
                const auto& error=value->at("win32Error");const auto& elapsed=value->at("elapsedMs");
                if(!error.is_number_unsigned()||error>0xffffffffu||!elapsed.is_number_unsigned()||elapsed>duration)continue;
                if(!((state=="regular-file-metadata"&&error==0&&bytes.is_number_unsigned())||
                    (state=="non-regular-path"&&error==0&&bytes.is_null())||
                    (state=="not-found-at-lookup"&&(error==2||error==3)&&bytes.is_null())||
                    (state=="attribute-query-failed"&&error!=2&&error!=3&&bytes.is_null())))continue;
                const auto& cleanup=attempt.at("cleanupEvidence");const auto& sites=cleanup.at("sites");
                if(cleanup.at("schema")!=1||cleanup.at("scope")!="original-five-filesystem-cleanup-sites;not-path-absence-or-resource-return"||
                    !sites.is_array()||sites.size()!=5||sites[2].at("site")!="promoted-staging"||sites[2].at("attempted")!=false)continue;
                EvidenceJson errors=EvidenceJson::array();
                bool nonzero=false;
                for(std::size_t j=3;j<5;++j){
                    const auto& site=sites[j];const auto& code=site.at("errorValue");const auto& category=site.at("errorCategory");
                    const bool codeValid=code.is_number_unsigned()?code.get<std::uint64_t>()<=INT32_MAX:
                        code.is_number_integer()&&code.get<std::int64_t>()>=INT32_MIN&&code.get<std::int64_t>()<=INT32_MAX;
                    if(site.at("site")!=(j==3?"failed-temporary":"failed-staging")||site.at("attempted")!=true||
                        !codeValid||
                        (category!="system"&&category!="generic"&&category!="other"))break;
                    errors.push_back({{"errorValue",code},{"errorCategory",category}});
                    nonzero|=code!=0;
                }
                if(errors.size()!=2||!nonzero)continue;
                lookups.push_back({{"attempt",i+1},{"failedCleanupErrors",std::move(errors)},
                    {"failedTemporaryAfterCleanup",*value}});
            }catch(...){ }
        }
        if(!lookups.empty())(*steps)["failedTemporaryLookups"].swap(lookups);
    }catch(...){ }
}

inline void addPriorSnapshotCopyFailure(EvidenceJson& receipt,const EvidenceJson& row) noexcept {
    constexpr std::size_t maxBytes=4096;
    try {
        const auto found=row.find("snapshotCopyFailureEvidence");
        if(found==row.end()||!found->is_object()||found->size()!=10)return;
        const auto& evidence=*found;
        const auto uintValue=[](const EvidenceJson& value){
            if(!value.is_number_unsigned()&&(!value.is_number_integer()||value.get<std::int64_t>()<0))
                throw std::domain_error("not an unsigned value");
            return value.get<std::uint64_t>();
        };
        const auto observed=uintValue(evidence.at("failureObservations"));
        if(observed==0)return;
        const auto& ok=row.at("ok");
        const auto attempts=uintValue(row.at("attempts"));
        if(!ok.is_boolean()||attempts==0||attempts>2)return;
        const auto failedAttempts=attempts-(ok==true?1:0);
        if(observed>failedAttempts||uintValue(evidence.at("schema"))!=1||
            evidence.at("scope")!="mandatory-shared-copy-failures-in-this-export-call"||
            evidence.at("sourcePathsOmitted")!=true||evidence.at("sourceStabilityDurabilityOrResourceReturnProven")!=false||
            uintValue(evidence.at("maxRetainedFailures"))!=2)return;
        const auto built=uintValue(evidence.at("evidenceBuildFailures"));
        const auto retained=uintValue(evidence.at("retainedFailures"));
        const auto& failures=evidence.at("failures");
        if(built>observed||retained!=observed-built||
            evidence.at("retainedObservedFailuresComplete")!=(built==0)||
            (failures.is_array()?failures.size()!=retained:!failures.is_null()||retained!=0))return;

        // Bound the shape before serializing or copying the optional payload.
        std::uint64_t previous=0;
        for(const auto& failure:failures){
            if(!failure.is_object()||failure.size()!=5)return;
            const auto attempt=uintValue(failure.at("attempt"));
            if(attempt<=previous||attempt>failedAttempts||uintValue(failure.at("win32Error"))>0xffffffffu)return;
            previous=attempt;
            uintValue(failure.at("listIndex"));
            const auto& entry=failure.at("entry").get_ref<const std::string&>();
            if(entry.empty()||entry.size()>1024||entry.find_first_of("/\\:\0",0,4)!=std::string::npos)return;
            const auto& copy=failure.at("copyObservation");
            if(!copy.is_object()||copy.size()<14||copy.size()>15||uintValue(copy.at("schema"))!=1||
                copy.at("scope")!="original-shared-copy-calls-only"||copy.at("copyComplete")!=false||
                copy.at("sourceStabilityProven")!=false||copy.at("durableWriteProven")!=false||
                copy.at("resourceReturnProven")!=false||!copy.at("destinationCreated").is_boolean()||
                !copy.at("sourceSizeKnown").is_boolean())return;
            const auto& stage=copy.at("stage").get_ref<const std::string&>();
            if(stage!="source-open"&&stage!="source-size"&&stage!="size-limit"&&stage!="destination-open"&&
                stage!="source-read"&&stage!="destination-write"&&stage!="destination-close")return;
            const auto& sourceBytes=copy.at("sourceBytesAtOpen");
            if(copy.at("sourceSizeKnown")==true)uintValue(sourceBytes);
            else if(!sourceBytes.is_null())return;
            for(const auto* key:{"successfulReadBytes","successfulWriteBytes","readCalls","writeCalls"})uintValue(copy.at(key));
            if(copy.size()==15){
                const char* boundary=entry=="monitor-detail.log"?"monitorWriterBoundary":
                    entry=="virtual-gamepad.log"?"virtualGamepadWriterBoundary":
                    entry=="focus-trace.log"?"focusTraceWriterBoundary":
                    entry=="input-capture.jsonl"?"inputCaptureWriterBoundary":
                    entry=="tdp.log"?"tdpWriterBoundary":entry=="tdp-detail.log"?"tdpDetailWriterBoundary":
                    entry=="native-detail.log"?"nativeDetailWriterBoundary":nullptr;
                if(!boundary)return;
                const auto& state=copy.at(boundary);
                if(state!="source-open-failed"&&state!="source-size-failed"&&
                    state!="captured-under-writer-lock"&&state!="flush-failed")return;
            }
        }
        if(evidence.dump().size()<=maxBytes){
            EvidenceJson bounded=evidence;
            receipt["snapshotCopyFailureEvidence"].swap(bounded);
        }
    }catch(...){receipt.erase("snapshotCopyFailureEvidence");}
}

inline void retainPriorOrdinaryTerminal(EvidenceJson& slot,const EvidenceJson& row,
    const EvidenceJson* returned) noexcept {
    try {
        EvidenceJson terminal=EvidenceJson::object();
        for(const char* key:{"ok","cleanupOk","bodyReturned","attempts","exportElapsedMs",
                "archiveElapsedMs","phaseTimingBuildFailures","win32Error","reason","completion",
                "terminalStage","exportMode","exceptionClass"}){
            const auto value=row.find(key);
            if(value!=row.end()&&(value->is_boolean()||value->is_number_unsigned()||
                    (value->is_number_integer()&&value->get<long long>()>=0)||
                    (value->is_string()&&value->get_ref<const std::string&>().size()<=256)))
                terminal[key]=*value;
        }
        const auto phases=row.find("phaseElapsedMs");
        if(phases!=row.end()&&phases->is_object()){
            for(const char* key:{"flush","steamDiscovery","cachedStateAndFileDiscovery","windowsSystemPowerReport",
                    "remainingStateAndManifest","windowsReportAndRemainingState","manifestWrite",
                    "availabilityAndDestination","snapshotIndexAndArchive"}){
                const auto value=phases->find(key);
                if(value!=phases->end()&&value->is_number_unsigned())terminal["phaseElapsedMs"][key]=*value;
            }
        }
        if(returned&&returned->is_object()){
            const auto ok=returned->find("ok");
            if(ok!=returned->end()&&ok->is_boolean())terminal["bodyResultOk"]=*ok;
        }
        EvidenceJson receipt={{"schema",1},{"exportOperation",row.at("exportOperation")},
            {"terminal",std::move(terminal)}};
        // Carry the original first failure, never the full attempt or file lists.
        try {
            const auto retry=row.find("previousArchiveAttempt"), attempts=row.find("attempts");
            if(attempts!=row.end()&&attempts->is_number_integer()&&*attempts==2&&
                retry!=row.end()&&retry->is_object()&&retry->size()==8){
                const auto code=[&](const char* key){
                    const auto value=retry->find(key);
                    return value!=retry->end()&&value->is_number_integer()&&*value>=0&&*value<=0xffffffffu;
                };
                const auto reason=retry->find("reason"),schema=retry->find("schema"),attempt=retry->find("attempt"),
                    scope=retry->find("scope"),clean=retry->find("cleanupOk");
                if(reason!=retry->end()&&reason->is_string()&&!reason->get_ref<const std::string&>().empty()&&
                    reason->get_ref<const std::string&>().size()<=63&&schema!=retry->end()&&schema->is_number_integer()&&*schema==1&&
                    attempt!=retry->end()&&attempt->is_number_integer()&&*attempt==1&&
                    scope!=retry->end()&&*scope=="previous-attempt-only;not-current-export-outcome"&&
                    clean!=retry->end()&&clean->is_boolean()&&code("win32Error")&&code("temporaryRemoveError")&&code("stagingRemoveError"))
                    receipt["previousArchiveAttempt"]=*retry;
            }
        }catch(...){receipt.erase("previousArchiveAttempt");}
        addPriorFinalOutputCheck(receipt,row);
        addPriorArchiveSteps(receipt,row);
        addPriorArchiveChildFailures(receipt,row);
        addPriorArchiveTemporaryLookups(receipt,row);
        addPriorSnapshotCopyFailure(receipt,row);
        slot.swap(receipt);
    }catch(...){ }
}

inline void addPriorOrdinaryTerminal(EvidenceJson& availability,EvidenceJson& prior,
    const EvidenceJson* rendererArgs=nullptr) noexcept {
    if(prior.is_null())return;
    try{
        if(rendererArgs&&rendererArgs->is_object()){
            const auto reply=rendererArgs->find("priorOrdinaryExportReply");
            if(reply!=rendererArgs->end()&&reply->is_object()&&reply->size()==4){
                const auto& schema=reply->at("schema");
                const auto& op=reply->at("exportOperation");
                const auto& replyOk=reply->at("replyOk");
                const auto& elapsed=reply->at("elapsedMs");
                const auto& terminal=prior.at("terminal");
                if(schema.is_number_integer()&&schema==1&&op.is_object()&&
                    op.at("schema").is_number_integer()&&op.at("writerPid").is_number_integer()&&
                    op.at("writerProcessCreation100ns").is_string()&&op==prior.at("exportOperation")&&
                    replyOk.is_boolean()&&terminal.at("bodyReturned")==true&&replyOk==terminal.at("bodyResultOk")&&
                    elapsed.is_number_integer()&&elapsed>=0&&elapsed<=9007199254740991ULL){
                    prior["rendererReply"]={{"schema",1},{"replyOk",replyOk},{"elapsedMs",elapsed}};
                }
            }
        }
    }catch(...){ } // Optional renderer evidence must not suppress the Native carry.
    try{availability["priorOrdinaryExportTerminal"].swap(prior);}catch(...){ }
}

// Only values captured at the original copier calls; serialization adds no IO.
template<class CopyEvidence>
inline EvidenceJson snapshotCopyEvidence(const CopyEvidence& value) {
    EvidenceJson result={{"schema",1},{"scope","original-shared-copy-calls-only"},{"stage",value.stage},
        {"sourceSizeKnown",value.sourceSizeKnown},
        {"sourceBytesAtOpen",value.sourceSizeKnown?EvidenceJson(value.sourceBytesAtOpen):EvidenceJson(nullptr)},
        {"successfulReadBytes",value.successfulReadBytes},{"successfulWriteBytes",value.successfulWriteBytes},
        {"readCalls",value.readCalls},{"writeCalls",value.writeCalls},
        {"destinationCreated",value.destinationCreated},{"copyComplete",value.copyComplete},
        {"sourceStabilityProven",false},{"durableWriteProven",false},{"resourceReturnProven",false}};
    if(value.writerBoundaryField&&value.writerBoundaryState)result[value.writerBoundaryField]=value.writerBoundaryState;
    return result;
}
inline EvidenceJson bufferedLogEvidence(const char* file,const ymcc::logging::BufferedDomainLog::Evidence& value) {
    const auto failure=[](const ymcc::logging::BufferedDomainLog::FailureRecord& record)->EvidenceJson {
        if(!record.stage)return nullptr;
        return {{"stage",record.stage},{"tickKnown",record.tickKnown},
            {"tick",record.tickKnown?EvidenceJson(record.tick):EvidenceJson(nullptr)},
            {"bufferedRowsAtObservation",record.bufferedRows},
            {"errorCodeObserved",record.errorCode!=0},{"errorCode",record.errorCode?EvidenceJson(record.errorCode):EvidenceJson(nullptr)}};
    };
    return {{"file",file},{"failureObservations",value.failureObservations},
        {"counterSaturated",value.failureObservations==ymcc::logging::BufferedDomainLog::CounterLimit},
        {"pendingRows",value.pendingRows},{"streamOpen",value.streamOpen},{"streamGood",value.streamGood},
        {"tailSeparatorEvidence",{{"schemaVersion",1},{"accepted",value.tailSeparators},
            {"last",value.tailSeparators ? EvidenceJson{{"tick",value.lastTailSeparatorTick},
                {"offsetBytes",value.lastTailSeparatorOffset}} : EvidenceJson(nullptr)}}},
        {"first",failure(value.first)},{"last",failure(value.last)}};
}
inline EvidenceJson rendererExportCounter(const EvidenceJson& args,const char* key) {
    EvidenceJson out={{"state","not-reported"},{"known",false},{"value",nullptr}};
    if(!args.is_object()){out["state"]="invalid-export-args";return out;}
    const auto value=args.find(key);
    if(value==args.end())return out;
    if(value->is_null()){out["state"]="reported-null";return out;}
    if(!value->is_number_integer()){out["state"]="invalid-type";return out;}
    if(*value<0||*value>9007199254740991ULL){out["state"]="out-of-range";return out;}
    out["state"]="reported";out["known"]=true;out["value"]=*value;return out;
}
inline EvidenceJson rendererExportCounterEvidence(const EvidenceJson& args) {
    return {{"schema",1},{"scope","renderer-reported-export-arguments;not-native-write-proof"},
        {"counterLimit",9007199254740991ULL},{"snapshotAtomicWithNative",false},{"nativeWriteOutcome","UNKNOWN"},
        {"frontendDiagnosticDeliveryFailures",rendererExportCounter(args,"frontendDiagnosticDeliveryFailures")},
        {"tdpDiagnosticDeliveryFailures",rendererExportCounter(args,"tdpDiagnosticDeliveryFailures")}};
}
inline std::string rendererExportCounterText(const EvidenceJson& evidence) {
    // Input is the fixed projection above. Missing/invalid counts never become 0.
    const auto value=evidence.is_object()?evidence.find("value"):evidence.end();
    return value!=evidence.end()&&value->is_number_integer()&&*value>=0&&*value<=9007199254740991ULL
        ?value->dump():"not-observed";
}
inline EvidenceJson tdpRendererDeliveryEvidence(const EvidenceJson& source) {
    EvidenceJson result={{"schema",1},{"scope","renderer-reported-TDP-diagnostic-sender;not-Native-write-or-TDP-control-proof"},
        {"status","missing-or-unsupported"},{"reported",EvidenceJson::object()},
        {"nativeWriteOutcome","UNKNOWN"},{"requestRecoveryTriggered",false},{"snapshotAtomicWithNative",false}};
    if(!source.is_object())return result;
    const auto schema=source.find("schema");
    if(schema==source.end()||!schema->is_number_integer()||*schema!=1)return result;
    result["status"]="projected";
    for(const auto* field:{"maxInFlightRequests","maxRowsPerRequest","inFlightRequests","inFlightRows","unconfirmedRows",
        "offeredRequests","offeredRows","admittedRequests","admittedRows","ackAcceptedRequests","ackAcceptedRows",
        "nativeNotOkRequests","nativeNotOkRows","unrecognizedReplyRequests","unrecognizedReplyRows",
        "invokeRejectedRequests","invokeRejectedRows","capacityRejectedRequests","capacityRejectedRows","senderExceptionRows","highWaterRequests"})
        result["reported"][field]=rendererExportCounter(source,field)["value"];
    return result;
}
inline EvidenceJson rendererOverlaySessionEntryEvidence(const EvidenceJson* source) {
    EvidenceJson result={{"schema",1},{"status","not-reported"},{"known",false},
        {"scope","renderer-reported-session-entry-counts-only"},{"counterLimit",9007199254740991ULL},
        {"counts",EvidenceJson::object()},{"atLimit",EvidenceJson::object()},{"counterFieldsKnown",0},
        {"snapshotAtomicWithNative",false},{"nativeAdmissionProven",false},{"actionOrDeliveryProven",false},{"zeroIsNoInputProof",false}};
    for(const auto* key:{"activePass","activeReject","inactivePass","inactiveReject"}){result["counts"][key]=nullptr;result["atLimit"][key]=nullptr;}
    if(!source)return result;
    if(source->is_null()){result["status"]="reported-null";return result;}
    if(!source->is_object()){result["status"]="invalid-shape";return result;}
    const auto schema=source->find("schema");
    if(schema==source->end()||!schema->is_number_integer()||*schema!=1){result["status"]="unsupported-schema";return result;}
    const auto limit=source->find("counterLimit"),scope=source->find("scope");
    if(limit==source->end()||!limit->is_number_integer()||*limit!=9007199254740991ULL||
        scope==source->end()||!scope->is_string()||*scope!="completed-renderer-native-ui-input-session-gate;module-lifetime"){
        result["status"]="unsupported-scope-or-limit";return result;
    }
    const auto counts=source->find("counts");unsigned known=0;
    if(counts!=source->end()&&counts->is_object())for(const auto* key:{"activePass","activeReject","inactivePass","inactiveReject"}){
        const auto value=counts->find(key);
        if(value==counts->end()||!value->is_number_integer()||*value<0||*value>9007199254740991ULL)continue;
        result["counts"][key]=*value;result["atLimit"][key]=*value==9007199254740991ULL;++known;
    }
    result["counterFieldsKnown"]=known;result["known"]=known==4;result["status"]=known==4?"projected":"partial-counters";
    return result;
}
inline EvidenceJson rendererDeliveryEvidence(const EvidenceJson& source) {
    // Project fixed numeric counters only. Older renderers remain explicitly unknown.
    EvidenceJson result={{"schema",1},{"scope","renderer-reported-counters;not-native-write-proof"},
        {"snapshotAtomicWithNative",false},{"nativeWriteOutcome","UNKNOWN"},
        {"status","missing-or-unsupported"},{"failureCount",nullptr},{"channels",EvidenceJson::array()}};
    result["overlaySessionEntry"]=rendererOverlaySessionEntryEvidence(nullptr);
    if(!source.is_object())return result;
    const auto schema=source.find("schema");
    if(schema==source.end()||!schema->is_number_integer()||*schema!=1)return result;
    result["status"]="projected";
    const auto overlayEntry=source.find("overlaySessionEntry");
    result["overlaySessionEntry"]=rendererOverlaySessionEntryEvidence(overlayEntry==source.end()?nullptr:&*overlayEntry);
    const auto counter=[](const EvidenceJson& object,const char* name)->EvidenceJson {
        const auto it=object.find(name);
        if(it==object.end()||!it->is_number_integer()||*it<0||*it>9007199254740991ULL)return nullptr;
        return *it;
    };
    result["failureCount"]=counter(source,"failureCount");
    const auto admission=source.find("senderAdmission");
    const bool admissionKnown=admission!=source.end()&&admission->is_object()&&
        admission->contains("schema")&&(*admission)["schema"].is_number_integer()&&(*admission)["schema"]==1;
    EvidenceJson admissionView={{"schema",1},{"status",admissionKnown?"projected":"missing-or-unsupported"},
        {"reported",EvidenceJson::object()},{"scope","renderer-diagnostic-admission;not-native-write-or-input-proof"}};
    for(const auto* field:{"maxInFlightRequests","inFlightRequests","offeredRequests","admittedRequests",
        "settledRequests","capacityRejectedRequests","highWaterRequests"})
        admissionView["reported"][field]=admissionKnown?counter(*admission,field):EvidenceJson(nullptr);
    result["senderAdmission"]=std::move(admissionView);
    const auto rows=source.find("channels");
    for(const auto* name:{"frontend-error","gamepad-admission","controller-preview","controller-calibration","cpu-ccd-visibility","other-trace"}){
        const EvidenceJson* match=nullptr;unsigned matches=0;
        if(rows!=source.end()&&rows->is_array()&&rows->size()<=6){
            for(const auto& row:*rows){
                if(!row.is_object())continue;
                const auto channel=row.find("channel");
                if(channel!=row.end()&&channel->is_string()&&*channel==name){match=&row;++matches;}
            }
        }
        const auto* counts=match&&matches==1&&match->contains("counts")&&(*match)["counts"].is_object()?&(*match)["counts"]:nullptr;
        EvidenceJson projected=EvidenceJson::object();unsigned fieldsKnown=0;
        for(const auto* outcome:{"native-not-ok","response-timeout","invoke-rejected","sender-exception"}){
            auto value=counts?counter(*counts,outcome):EvidenceJson(nullptr);
            if(!value.is_null())++fieldsKnown;
            projected[outcome]=std::move(value);
        }
        result["channels"].push_back({{"channel",name},{"counts",std::move(projected)},
            {"sourceMatches",matches},{"countsObjectPresent",counts!=nullptr},{"counterFieldsKnown",fieldsKnown},{"known",fieldsKnown==4},
            {"capacityRejectedRequests",match&&matches==1?counter(*match,"capacityRejectedRequests"):EvidenceJson(nullptr)}});
    }
    return result;
}
inline std::string missingFileReason(const std::error_code& error) {
    if(!error || error==std::errc::no_such_file_or_directory || error.value()==ERROR_FILE_NOT_FOUND || error.value()==ERROR_PATH_NOT_FOUND)
        return "not-created-or-not-retained";
    return "file-status-error";
}
inline bool logEvidenceNumber(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(),text.end(),[](char c){return c>='0'&&c<='9';});
}
inline std::string canonicalLogEvidenceName(std::string name) {
    const auto dot=name.rfind('.');
    if(dot!=std::string::npos){
        const auto suffix=name.substr(dot+1);
        if(suffix=="prev"||suffix=="previous"||suffix=="pending"||logEvidenceNumber(suffix))name.resize(dot);
    }
    // FanHost uses name.1.log; native domains use name.log.1.
    if(name.size()>4 && name.compare(name.size()-4,4,".log")==0){
        const auto prefix=name.substr(0,name.size()-4);const auto marker=prefix.rfind('.');
        if(marker!=std::string::npos){const auto suffix=prefix.substr(marker+1);
            if(suffix=="prev"||suffix=="previous"||suffix=="pending"||logEvidenceNumber(suffix))name=prefix.substr(0,marker)+".log";}
    }
    return name;
}
inline bool jsonLinesEvidenceName(std::string name) {
    name=canonicalLogEvidenceName(std::move(name));
    for(const auto* exact:{"input-capture.jsonl","native-lifecycle.log","model-adaptation.log","virtual-adaptation.log","virtual-adaptation-session.jsonl","model-adaptation-session.jsonl","model-adaptation-signals-session.jsonl","native-detail.log","monitor-detail.log","tdp.log","tdp-detail.log",
        "frontend-errors.log","frontend-trace.log","frontend-trace-detail.log","webview-failures.log","fan-api.log",
        "fan-lifecycle.log","gyro-motion.log","gyro-motion-samples.log","virtual-gamepad.log","focus-trace.log","hardware-coordinator.log",
        "input-host-fatal.log","input-host-rejects.log","input-host-cost.log","input-host-signal.log","input-host-report-chain.log","input-host-xbox-sdk.log","physical-source-reader.log",
        "recovery-service.log","steam-original-recovery.jsonl","fan-host-fault.log","yeman-fan-host-runtime.log","sleep-facts.log",
        "battery-charge-client.log","battery-charge-host.log","msi-controller-visibility.log"})if(name==exact)return true;
    const std::string prefix="msi-controller-visibility-wake-";
    if(name.compare(0,prefix.size(),prefix)==0 && name.size()>prefix.size()+4 && name.compare(name.size()-4,4,".log")==0)
        return logEvidenceNumber(name.substr(prefix.size(),name.size()-prefix.size()-4));
    return false; // Steam/HC/app text logs are NOT malformed JSONL.
}
inline bool jsonDocumentEvidenceName(std::string name) {
    name=canonicalLogEvidenceName(std::move(name));
    return name.size()>=5 && name.compare(name.size()-5,5,".json")==0;
}
inline EvidenceJson stagedInspectionFailure(const std::string& entry,const char* kind) {
    EvidenceJson row=EvidenceJson::object();
    row["entry"]=entry;row["status"]="staged-inspection-error";
    row["originalBytesRetained"]=true;row["inspectionFailureKind"]=kind;
    return row;
}
// Validate the complete original JSON range while retaining only the five
// top-level string values used by inspection metadata. Duplicate keys keep
// their last value, including a later non-string that invalidates a string.
struct StagedJsonTimeSax final : nlohmann::json_sax<EvidenceJson> {
    static constexpr const char* fields[5]={"timestampUtc","timeWithOffset","timestampLocal","atUtc","time"};
    std::array<std::string,5> values;
    std::array<bool,5> strings{};
    size_t depth=0;
    int pending=-1;
    bool rootObject=false;
    void reset() noexcept {
        // A failed parse may leave an open container or a pending key.
        // Keep string capacity, but make every previous value unobservable.
        strings.fill(false);depth=0;pending=-1;rootObject=false;
    }
    void nonString() noexcept {
        if(rootObject&&depth==1&&pending>=0)strings[static_cast<size_t>(pending)]=false;
        pending=-1;
    }
    bool null() override {nonString();return true;}
    bool boolean(bool) override {nonString();return true;}
    bool number_integer(number_integer_t) override {nonString();return true;}
    bool number_unsigned(number_unsigned_t) override {nonString();return true;}
    bool number_float(number_float_t,const string_t&) override {nonString();return true;}
    bool string(string_t& value) override {
        if(rootObject&&depth==1&&pending>=0){values[static_cast<size_t>(pending)]=value;strings[static_cast<size_t>(pending)]=true;}
        pending=-1;return true;
    }
    bool binary(binary_t&) override {nonString();return true;}
    bool start_object(std::size_t) override {if(!depth)rootObject=true;else nonString();++depth;return true;}
    bool key(string_t& value) override {
        pending=-1;
        if(rootObject&&depth==1)for(size_t i=0;i<5;++i)if(value==fields[i]){pending=static_cast<int>(i);break;}
        return true;
    }
    bool end_object() override {if(depth)--depth;pending=-1;return true;}
    bool start_array(std::size_t) override {nonString();++depth;return true;}
    bool end_array() override {if(depth)--depth;pending=-1;return true;}
    bool parse_error(std::size_t,const std::string&,const nlohmann::detail::exception&) override {return false;}
};
inline bool stagedJsonLineTime(const char* first,const char* last,StagedJsonTimeSax& sax,
        const std::string*& time,const char*& field) {
    sax.reset();time=nullptr;field=nullptr;
    if(!EvidenceJson::sax_parse(first,last,&sax,EvidenceJson::input_format_t::json,true,false))return false;
    for(size_t i=0;i<5;++i)if(sax.strings[i]){time=&sax.values[i];field=StagedJsonTimeSax::fields[i];break;}
    return true;
}

inline EvidenceJson stagedFileEvidence(const std::filesystem::path& staged,const std::string& entry) noexcept {
    try {
        EvidenceJson row={{"entry",entry},{"evidenceScope","actual-staged-bytes-not-live-source"},{"originalBytesRetained",true}};
        std::ifstream in(staged,std::ios::binary);if(!in){row["status"]="staged-read-error";return row;}
        in.seekg(0,std::ios::end);const auto end=in.tellg();if(end<0){row["status"]="staged-size-error";return row;}
        const auto size=static_cast<unsigned long long>(end);row["bytes"]=size;
        const bool jsonl=jsonLinesEvidenceName(entry);
        if(!jsonl){
            char last=0;
            if(size){
                // Keep the original size observation even if the file grows.
                in.seekg(static_cast<std::streamoff>(size-1));in.get(last);
                if(!in){row["status"]="staged-read-error";return row;}
            }
            row["earlierBytesNotInspected"]=size?size-1:0;
            row["inspectionScope"]=size?"final-byte-only":"empty-file";
            row["format"]="opaque-file";row["endsWithNewline"]=size==0||last=='\n';
            row["status"]="inspected";return row;
        }

        constexpr unsigned long long limit=8ULL*1024*1024;
        const auto offset=size>limit?size-limit:0ULL;
        const auto length=static_cast<size_t>(size-offset);
        auto storage=length?std::make_unique_for_overwrite<char[]>(length):nullptr;
        in.seekg(static_cast<std::streamoff>(offset));
        if(length)in.read(storage.get(),static_cast<std::streamsize>(length));
        if(!in){row["status"]="staged-read-error";return row;}
        // Never expose an unfilled buffer to the JSONL parser.
        const auto text=length?std::string_view(storage.get(),length):std::string_view{};
        row["earlierBytesNotInspected"]=offset;row["inspectionScope"]=offset?"retained-tail-only":"retained-file-only";
        row["format"]="json-lines";
        row["endsWithNewline"]=size==0 || (!text.empty()&&text.back()=='\n');row["status"]="inspected";
        size_t cursor=0;if(offset){const auto first=text.find('\n');cursor=first==std::string::npos?text.size():first+1;row["leadingBoundaryLineNotInspected"]=true;}
        unsigned long long valid=0,invalid=0,oversize=0;std::string firstTime,lastTime,firstTimeField,lastTimeField;
        bool partialTail=false,oversizeTail=false;
        StagedJsonTimeSax timeStorage;
        while(cursor<text.size()){
            const auto newline=text.find('\n',cursor);const bool terminal=newline==std::string::npos;
            const size_t count=terminal?text.size()-cursor:newline-cursor;
            if(count>65536){++oversize;if(terminal)oversizeTail=true;}
            else if(count){
                try {const std::string* time=nullptr;const char* timeField=nullptr;
                    if(stagedJsonLineTime(text.data()+cursor,text.data()+cursor+count,timeStorage,time,timeField)){++valid;
                        if(time&&!time->empty()){if(firstTime.empty()){firstTime=*time;firstTimeField=timeField;}lastTime=*time;lastTimeField=timeField;}
                    }else{++invalid;if(terminal)partialTail=true;}
                }catch(const EvidenceJson::exception&){++invalid;if(terminal)partialTail=true;}
            }
            if(terminal)break;cursor=newline+1;
        }
        row["validJsonRows"]=valid;row["invalidJsonRows"]=invalid;row["oversizeRowsNotParsed"]=oversize;
        row["firstTime"]=firstTime.empty()?"not-observed":firstTime;row["lastTime"]=lastTime.empty()?"not-observed":lastTime;
        row["firstTimeField"]=firstTimeField.empty()?EvidenceJson(nullptr):EvidenceJson(firstTimeField);
        row["lastTimeField"]=lastTimeField.empty()?EvidenceJson(nullptr):EvidenceJson(lastTimeField);
        row["timeScope"]="first-last-in-inspected-file-order;field-names-retained;legacy-time-zone-not-inferred";
        row["partialOrInvalidTrailingRecord"]=partialTail;row["oversizeTrailingRecordNotParsed"]=oversizeTail;
        row["recordBoundary"]=partialTail?"partial-or-invalid-tail-retained":oversizeTail?"oversize-tail-retained-not-parsed":row["endsWithNewline"].get<bool>()?"line-terminated":"valid-json-without-final-newline";
        if(partialTail)row["status"]="snapshot-partial-tail-retained";
        row["interpretation"]="file-snapshot-not-atomic-transaction; partial-tail-is-not-proof-of-source-corruption";
        return row;
    }catch(const std::bad_alloc&){return stagedInspectionFailure(entry,"allocation-exception");}
    catch(const std::exception&){return stagedInspectionFailure(entry,"standard-exception");}
    catch(...){return stagedInspectionFailure(entry,"unknown-exception");}
}
} // namespace ymcc::logexport
