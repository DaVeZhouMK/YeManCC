#pragma once
#include "decky_frontend_state.h"
#include "decky_sidebar_health.h"
// Low-frequency diagnostics have an explicit field allowlist. Bootstrap tokens,
// request args, endpoints, config contents and game identities never enter logs.
#include <limits>
#include <cstring>
#include <string>
#include <string_view>
#include "json.hpp"
#include "steam_failure_diagnostics.h"
namespace ymcc::deckydiag {
using Json=nlohmann::json;
// This note borrows the synchronous mode read's existing checks. It does not
// recheck a retired scope or assign a cause when none of these checks refused.
class ModeReadCancelNote {
    const char* first_=nullptr;
    bool refuse(const char* check) noexcept { if(!first_)first_=check;return false; }
public:
    template<class ObserverCurrent,class OwnerAlive,class PowerCurrent,class IdentityCurrent>
    bool current(const ObserverCurrent& observer,const OwnerAlive& owner,const PowerCurrent& power,
                 const IdentityCurrent& identity) noexcept {
        try {
            if(!observer())return refuse("observer-fence");
            if(!owner())return refuse("owner-exit");
            if(!power())return refuse("power-scope");
            if(!identity())return refuse("identity-epoch");
            return true;
        } catch (...) { return refuse("check-error"); }
    }
    void attach(Json& detail, unsigned long long generation, unsigned long long suspendSerial) const {
        if(!first_||!generation||detail.value("reason",Json{})!="shared-mode-read-cancelled")return;
        detail["modeReadCancelCheck"]=first_;
        detail["modeReadStartPowerGeneration"]=generation;
        detail["modeReadStartSuspendSerial"]=suspendSerial;
    }
};
inline bool shouldLogSupersededModeRead(bool enabled, unsigned long loaderPid,
                                       std::string_view reason, std::string_view detail) noexcept {
    return enabled && !loaderPid && reason=="steam-ui-state-pending" &&
        (detail=="shared-mode-pending" || detail=="shared-mode-events-unsettled");
}
template<class Read>
inline Json supersededModeReadFields(unsigned long steamPid, unsigned long long steamBirth, const Read& read) {
    Json fields{{"steamPid",steamPid},{"steamBirth",std::to_string(steamBirth)},
        {"hintCause",read.hintCause},{"hintOriginLine",read.hintOriginLine},{"hintCount",read.hintCount},
        {"readStartedTick",read.readStartedTick}};
    if(read.nextReadMs==(std::numeric_limits<decltype(read.nextReadMs)>::max)())fields["retryParked"]=true;
    else fields["nextReadMs"]=read.nextReadMs;
    return fields;
}
inline constexpr unsigned long long maxBytes=1024ull*1024ull;
inline Json cssModeHandoffRecord(const Json& raw) {
    if (!raw.is_object()) return {};
    const auto path = raw.find("path");
    if (path == raw.end() || (*path != "stable-notice" && *path != "changed-notice" && *path != "cache-fallback")) return {};
    const auto number = [&](const char* key) -> std::uint64_t {
        const auto value = raw.find(key);
        return value != raw.end() && value->is_number_unsigned() ? value->get<std::uint64_t>() : 0;
    };
    const auto generation = number("generation"), serial = number("suspendSerial"), sequence = number("notificationSequence"),
        accepted = number("acceptedTick"), returned = number("readerReturnedTick"), published = number("publishedTick"), enqueue = number("enqueuedTick");
    if (!generation || (!serial && !sequence) || !accepted) return {};
    Json bounded{{"path",*path},{"generation",generation},{"suspendSerial",serial},{"notificationSequence",sequence},{"acceptedTick",accepted}};
    if (returned && returned <= published && published <= accepted) {
        bounded["readerReturnedTick"] = returned; bounded["publishedTick"] = published;
        if (*path != "cache-fallback" && enqueue >= published && enqueue <= accepted) bounded["enqueuedTick"] = enqueue;
    }
    return bounded;
}
inline bool validBuildId(const Json& value) {
    if(!value.is_string())return false;
    const auto& text=value.get_ref<const std::string&>();if(text.size()!=64)return false;
    for(const auto c:text)if(!((c>='a'&&c<='f')||(c>='0'&&c<='9')))return false;
    return true;
}
// Renderer telemetry remains bounded facts only. Never arbitrary log content.
// Finite notification category only; never source payloads or authority.
inline const char* snapshotInvalidationSource(const Json& input) {
    if(!input.contains("invalidatedBy")||!input["invalidatedBy"].is_string())return nullptr;
    const auto& value=input["invalidatedBy"].get_ref<const std::string&>();
    for(const auto* source:{"audio","monitor","input","touchpads","frames","schedule","profile","game","power","fan","controller","steam","trainer","speed","lossless","action","unspecified","multiple"})if(value==source)return source;
    return nullptr;
}
// JS-reported first-client timing, never admission or a connection receipt.
// scheduledRetryMs sums requested delays, including cancelled waits; it is not
// measured sleep time. Unknown or malformed diagnostics are simply omitted.
inline Json clientStartupRecord(const Json& raw) {
    if(!raw.is_object()||raw.size()!=8||!raw.contains("schema")||!raw["schema"].is_number_integer()||raw["schema"]!=1)return Json();
    for(const auto* key:{"elapsedMs","bootstrapReadyMs","socketOpenMs","scheduledRetryMs","bootstrapMisses","socketAttempts","retryCount"}){
        const auto value=raw.find(key);if(value==raw.end()||!value->is_number_integer()||*value<0)return Json();
        const auto maximum=std::strcmp(key,"bootstrapMisses")==0||std::strcmp(key,"socketAttempts")==0||std::strcmp(key,"retryCount")==0?1000000u:3600000u;
        if(*value>maximum)return Json();
    }
    if(raw["bootstrapReadyMs"]>raw["elapsedMs"]||raw["socketOpenMs"]>raw["elapsedMs"])return Json();
    return raw;
}
inline void attachClientStartup(Json& detail,const Json& request,bool& recorded) noexcept {
    try{
        if(recorded||!request.is_object()||!request.contains("clientStartup"))return;
        auto facts=clientStartupRecord(request["clientStartup"]);if(facts.is_null())return;
        detail["clientStartup"]=std::move(facts);recorded=true;
    }catch(...){/* Optional logging never changes request acceptance. */}
}
inline void snapshotReadFailureFacts(Json& result,const Json& input) {
    if(const auto* source=snapshotInvalidationSource(input))result["invalidatedBy"]=source;
    const auto copy=[&](const char* key,std::initializer_list<const char*> allowed){
        if(!input.contains(key)||!input[key].is_string())return;
        const auto& value=input[key].get_ref<const std::string&>();
        for(const auto* item:allowed)if(value==item){result[key]=value;break;}
    };
    copy("failureKind",{"source-changed","read-error","owner-read"});
    copy("failureStage",{"audio","profile","power","powerSource","schedule","performance","topology","gyro","frames","touchpads","global","monitor","input"});
    copy("discardReason",{"disposed","peer-changed","demand-changed","source-changed"});
}
// Completed fixed snapshot subreads only; no paths, errors or new log stream.
inline void performanceReadTimings(Json& result,const Json& input) {
    for(const auto* key:{"audioMs","profileMs","performanceScheduleMs","performanceGameConfigMs","performancePowerModeMs","performanceGameMs","performanceCoreMs","performanceTdpCapabilityMs","performanceCpuProfilesMs","performanceFrameRatesMs"})
        if(input.contains(key)&&input[key].is_number_unsigned())result[key]=(std::min)(input[key].get<unsigned long long>(),3600000ull);
}
// Capture only already-read reservation facts. This POD never participates in
// admission and allocates nothing while the settings transaction is locked.
struct RecoveryBudgetFacts {
    bool observed=false,reopen=false,ordinaryKnown=false,ordinaryAvailable=false;
    bool upgradeAttempted=false,upgradeReserved=false,primaryWriteAccepted=false,backupWriteAccepted=false,copiesConfirmed=false;
    const char* source="unknown";const char* stage="eligibility";const char* outcome="rejected";
    const char* upgradeStage=nullptr; // Fixed producer literals; no paths/errors/identity payload.
    std::array<char,65> frontendBuildId{};
};
inline Json recoveryBudgetRecord(const RecoveryBudgetFacts& facts) {
    if(!facts.observed)return Json();
    Json row={{"schema",1},{"operation",facts.reopen?"reopen-bigpicture":"rebuild-ui-document"},
        {"source",facts.source},{"stage",facts.stage},{"outcome",facts.outcome},
        {"upgradeAttempted",facts.upgradeAttempted},{"upgradeReserved",facts.upgradeReserved},
        {"primaryWriteAccepted",facts.primaryWriteAccepted},{"backupWriteAccepted",facts.backupWriteAccepted},{"copiesConfirmed",facts.copiesConfirmed}};
    if(facts.upgradeAttempted&&facts.upgradeStage)row["upgradeStage"]=facts.upgradeStage;
    if(facts.ordinaryKnown)row["ordinaryAvailable"]=facts.ordinaryAvailable;
    if(facts.frontendBuildId[0])row["frontendBuildId"]=facts.frontendBuildId.data();
    return row;
}
// Child metadata is diagnostic only: malformed/unknown facts are omitted,
// never converted into a protocol failure, new allowance or dispatch receipt.
inline Json normalizeRecoveryBudget(const Json& raw) {
    if(!raw.is_object()||raw.size()>13||!raw.contains("schema")||!raw.at("schema").is_number_integer()||raw.at("schema")!=1)return Json();
    const auto selected=[&](const char* key,std::initializer_list<const char*> allowed){
        const auto it=raw.find(key);if(it==raw.end()||!it->is_string())return false;
        const auto& value=it->get_ref<const std::string&>();for(const auto* item:allowed)if(value==item)return true;return false;
    };
    if(!selected("operation",{"rebuild-ui-document","reopen-bigpicture"})||
       !selected("source",{"unknown","ordinary","verified-upgrade"})||
       !selected("stage",{"eligibility","history","reservation","upgrade","primary-write","backup-write","readback","complete"})||
       !selected("outcome",{"rejected","uncertain","reserved"}))return Json();
    Json row={{"schema",1},{"operation",raw.at("operation")},{"source",raw.at("source")},{"stage",raw.at("stage")},{"outcome",raw.at("outcome")}};
    for(const auto* key:{"ordinaryAvailable","upgradeAttempted","upgradeReserved","primaryWriteAccepted","backupWriteAccepted","copiesConfirmed"}){
        const auto it=raw.find(key);if(it!=raw.end()){if(!it->is_boolean())return Json();row[key]=*it;}
    }
    if(raw.contains("upgradeStage")){
        if(!raw.value("upgradeAttempted",false)||!selected("upgradeStage",{"lease","native-marker","protected-directory","native-receipt","native-image","native-file","native-build","history-read","history-merge","build-observe","history-value","history-primary","history-backup","history-readback","history-current","grant","final-current","reserved"}))return Json();
        row["upgradeStage"]=raw.at("upgradeStage");
    }
    if(raw.contains("frontendBuildId")){if(!validBuildId(raw.at("frontendBuildId")))return Json();row["frontendBuildId"]=raw.at("frontendBuildId");}
    if(row.size()!=raw.size())return Json();
    return row;
}
// Called only after the original reserve function releases SettingsFileGuard.
// Return the exact business result even if optional serialization fails.
inline void attachRecoveryBudget(Json& result,const RecoveryBudgetFacts& facts) noexcept {
    try{if(facts.observed)result["recoveryBudget"]=recoveryBudgetRecord(facts);}catch(...){}
}

// Startup diagnostics carry fixed phase names and observed monotonic durations
// only. They cannot authorize readiness, restoration, commands or retries.
inline Json startupPhaseRecord(const Json& args) {
    if(!args.is_object()||args.size()>5||!args.contains("stage")||!args["stage"].is_string()||
       !args.contains("elapsedMs")||!args["elapsedMs"].is_number_unsigned())return Json{};
    const auto phase=[](const Json& value){
        if(!value.is_string())return false;
        const auto& name=value.get_ref<const std::string&>();
        for(const auto* known:{"registered","native-context","fan-settings","ui-settings","placement","background-state","power-source","background-media","wake-state","fan-owner","boot-mode","performance","tray","ready"})if(name==known)return true;
        return false;
    };
    if(!phase(args["stage"]))return Json{};
    Json result={{"event","startup-phase"},{"stage",args["stage"]},{"elapsedMs",(std::min)(args["elapsedMs"].get<unsigned long long>(),3600000ull)}};
    const bool prior=args.contains("previousStage")||args.contains("previousMs");
    if(prior){
        if(!args.contains("previousStage")||!phase(args["previousStage"])||!args.contains("previousMs")||!args["previousMs"].is_number_unsigned())return Json{};
        result["previousStage"]=args["previousStage"];result["previousMs"]=(std::min)(args["previousMs"].get<unsigned long long>(),3600000ull);
    }
    return result;
}
// One optional field on the existing snapshot record; no arbitrary nested data.
inline const Json* snapshotProfileReadTimings(const Json& input) {
    const auto value=input.find("profileReadMs");
    if(value==input.end()||!value->is_object()||value->empty()||value->size()>4)return nullptr;
    for(auto it=value->begin();it!=value->end();++it){
        const auto& key=it.key();
        if((key!="queue"&&key!="settings"&&key!="main"&&key!="backup")||
            !it->is_number_unsigned()||*it>3600000ull)return nullptr;
    }
    return &*value;
}
inline Json frontendRecord(const Json& args) {
    if(!args.is_object()||!args.contains("event")||!args["event"].is_string())return Json{};
    const auto event=args["event"].get<std::string>();
    if(event=="startup-phase")return startupPhaseRecord(args);
    const auto* profile=event=="snapshot-read-finished"?snapshotProfileReadTimings(args):nullptr;
    if(args.size()>(event=="snapshot-read-finished"?32u+(snapshotInvalidationSource(args)?1u:0u)+(profile?1u:0u):22u))return Json{};
    if(event!="lossless-read-start"&&event!="lossless-read-timeout"&&event!="lossless-read-finished"&&
       event!="lossless-action-start"&&event!="lossless-action-timeout"&&event!="lossless-action-finished"&&
       event!="snapshot-demand"&&event!="snapshot-read-start"&&event!="snapshot-read-finished")return Json{};
    Json result={{"event",event}};
    for(const auto* key:{"elapsedMs","powerMs","powerSourceMs","scheduleMs","performanceMs","topologyMs","gyroMs","framesMs","touchpadsMs","globalMs","monitorMs","inputMs"})
        if(args.contains(key)&&args[key].is_number_unsigned())result[key]=(std::min)(args[key].get<unsigned long long>(),3600000ull);
    for(const auto* key:{"readId","demandEpoch"})
        if(args.contains(key)&&args[key].is_number_unsigned())result[key]=(std::min)(args[key].get<unsigned long long>(),9007199254740991ull);
    for(const auto* key:{"ok","ready","subscribe","discarded","completed","applied","pending"})if(args.contains(key)&&args[key].is_boolean())result[key]=args[key];
    if(args.contains("stage")&&args["stage"].is_string()){
        const auto stage=args["stage"].get<std::string>();if(stage=="settled"||stage=="late"||stage=="failed"||stage=="partial")result["stage"]=stage;
    }
    if(event=="snapshot-read-finished"){snapshotReadFailureFacts(result,args);performanceReadTimings(result,args);}
    if(profile)result["profileReadMs"]=*profile;
    return result;
}
// Native owner observations are fixed scalars only; they never certify the
// official socket, admission, health, or the remote close instant.
inline Json officialLoaderOwnerRecord(const Json& raw) {
    try{
        if(!raw.is_object()||!raw.contains("schema")||!raw["schema"].is_number_integer()||raw["schema"]!=1||!raw.contains("state")||!raw["state"].is_string())return nullptr;
        const auto& state=raw["state"].get_ref<const std::string&>();
        const auto natural=[&](const char* key){return raw.contains(key)&&raw[key].is_number_integer()&&raw[key]>=0&&raw[key]<=0xffffffffull;};
        if(!natural("error"))return nullptr;
        if(state=="unknown"||state=="absent"){
            if(raw.size()!=3||(state=="absent"&&raw["error"]!=0))return nullptr;return raw;
        }
        if(state!="running"&&state!="exited")return nullptr;
        if(!natural("pid")||raw["pid"]==0||!raw.contains("birth")||!raw["birth"].is_string())return nullptr;
        const auto& birth=raw["birth"].get_ref<const std::string&>();
        if(birth.empty()||birth.size()>20||birth.front()=='0'||birth.find_first_not_of("0123456789")!=std::string::npos||
           (birth.size()==20&&birth>"18446744073709551615"))return nullptr;
        if(state=="running")return raw.size()==5&&raw["error"]==0?raw:Json(nullptr);
        if(!raw.contains("exitCodeKnown")||!raw["exitCodeKnown"].is_boolean())return nullptr;
        if(raw["exitCodeKnown"].get<bool>())return raw.size()==7&&natural("exitCode")?raw:Json(nullptr);
        return raw.size()==6&&!raw.contains("exitCode")?raw:Json(nullptr);
    }catch(...){return nullptr;}
}

// Passive projection of facts already returned by the original inventory read.
inline Json shortcutReadRecord(const Json& raw) noexcept {
    try {
        if(!raw.is_object())return Json();
        Json row=Json::object();unsigned fields=0;
        for(const auto* key:{"sourceReadOnly","readAttempted","filePresent","regularFile","afterFailureReadProbe"}){
            const auto value=raw.find(key);if(value!=raw.end()&&value->is_boolean()){row[key]=*value;++fields;}
        }
        for(const auto* key:{"fileStatusErrorCode","fileSizeErrorCode"}){
            const auto value=raw.find(key);if(value==raw.end())continue;
            const bool valid=value->is_number_unsigned()?value->get<unsigned long long>()<=2147483647ull:
                value->is_number_integer()&&value->get<long long>()>=(-2147483647ll-1)&&value->get<long long>()<=2147483647ll;
            if(valid){row[key]=*value;++fields;}
        }
        for(const auto* key:{"fileBytes","afterFailureReadErrorCode"}){
            const auto value=raw.find(key);if(value==raw.end())continue;
            const bool nonnegative=value->is_number_unsigned()||(value->is_number_integer()&&value->get<long long>()>=0);
            const auto maximum=std::strcmp(key,"fileBytes")==0?(std::numeric_limits<unsigned long long>::max)():0xffffffffull;
            if(nonnegative&&value->get<unsigned long long>()<=maximum){row[key]=*value;++fields;}
        }
        const auto tag=raw.find("readErrorTag");
        if(tag!=raw.end()&&tag->is_string()){
            const auto& text=tag->get_ref<const std::string&>();
            for(const auto* allowed:{"media-shortcut-truncated","media-shortcut-string","media-shortcut-depth","media-shortcut-size","media-shortcut-duplicate","media-shortcut-type","media-shortcut-unterminated","media-shortcut-tail","media-shortcut-id","media-shortcut-gameid","media-shortcut-executable-unconfirmed","media-shortcuts-unavailable","media-shortcuts-changed","media-shortcuts-invalid","shortcut-read-exception"})
                if(text==allowed){row["readErrorTag"]=text;++fields;break;}
        }
        if(!fields)return Json();
        row["schema"]=1;row["scope"]="original-shortcut-inventory-read-facts;not-path-or-owner-proof";
        row["reportedFields"]=fields;row["pathsAndAccountsOmitted"]=true;row["filesystemErrorCategoryKnown"]=false;
        return row;
    }catch(...){return Json();}
}

inline Json record(const std::string& event,const Json& detail) {
    Json result={{"event",event}};
    if(!detail.is_object())return result;
    if(((event=="bootstrap-watch"&&detail.value("bootstrapStage",Json{})=="failed")||
        (event=="bootstrap"&&detail.value("ok",Json{})==false))&&
       detail.value("reason",Json{})=="mirror-bootstrap-cleanup-incomplete"){
        for(const auto* key:{"closeAccepted","finalNotification"})
            if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
    }
    if(event=="startup-phase"){const auto startup=startupPhaseRecord(detail);return startup.is_object()?startup:result;}
    if(event=="steam-mode-settled"){
        const auto wake=detail.find("admissionWake");
        if(wake!=detail.end()&&(*wake=="refresh-notified"||*wake=="retained-refresh-requested"||*wake=="final-read-recheck-queued"))
            result["admissionWake"]=*wake;
    }
    if(event=="steam-mode-read-superseded"){
        const auto cause=detail.find("hintCause"),birth=detail.find("steamBirth");
        if(cause!=detail.end()&&(*cause=="mode-refresh"||*cause=="connection-refresh"))result["hintCause"]=*cause;
        for(const auto* key:{"hintOriginLine","hintCount","readStartedTick","steamPid"})
            if(detail.contains(key)&&detail[key].is_number_unsigned())result[key]=detail[key];
        if(detail.value("retryParked",Json{})==true)result["retryParked"]=true;
        else if(const auto next=detail.find("nextReadMs");next!=detail.end()&&next->is_number_unsigned()&&
            next->get<std::uint64_t>()<(std::numeric_limits<std::uint32_t>::max)())result["nextReadMs"]=*next;
        if(birth!=detail.end()&&birth->is_string()){
            const auto& value=birth->get_ref<const std::string&>();
            if(!value.empty()&&value.size()<=20&&value.find_first_not_of("0123456789")==std::string::npos)result["steamBirth"]=*birth;
        }
        return result;
    }
    if(event=="steam-css-read"){
        result["readSchema"]=1;
        if(detail.contains("readOperation")&&detail["readOperation"].is_string()){
            const auto& operation=detail["readOperation"].get_ref<const std::string&>();
            if(operation=="identity"||operation=="documents")result["readOperation"]=operation;
        }
        for(const auto* key:{"readOk","readComplete"})if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        // Existing native document event only; no raw handle, URL or account.
        if(result.value("readOperation",std::string{})=="documents"){
            const auto natural=[&](const char* key,unsigned long long limit){return detail.contains(key)&&detail[key].is_number_unsigned()&&detail[key].get<unsigned long long>()<=limit;};
            if(result.value("readOk",false)&&natural("readTransportHeldTick",(std::numeric_limits<std::uint64_t>::max)()>>8)&&detail["readTransportHeldTick"]!=0u)
                result["readTransportHeldTick"]=detail["readTransportHeldTick"];
            if(detail.contains("readThreadCpuKnown")&&detail["readThreadCpuKnown"].is_boolean()){
                bool known=detail["readThreadCpuKnown"].get<bool>();
                const auto birth=detail.find("readThreadBirth");
                known=known&&natural("readThreadId",0xffffffffull)&&detail["readThreadId"]!=0u&&natural("readThreadCpu100ns",36000000000ull)&&
                    birth!=detail.end()&&birth->is_string()&&!birth->get_ref<const std::string&>().empty()&&birth->get_ref<const std::string&>().size()<=20&&
                    birth->get_ref<const std::string&>().find_first_not_of("0123456789")==std::string::npos&&birth->get_ref<const std::string&>().find_first_not_of('0')!=std::string::npos&&
                    (birth->get_ref<const std::string&>().size()<20||birth->get_ref<const std::string&>()<="18446744073709551615");
                result["readThreadCpuKnown"]=known;
                if(known)for(const auto* key:{"readThreadId","readThreadBirth","readThreadCpu100ns"})result[key]=detail[key];
            }
            if(detail.contains("readProcessHandlesKnown")&&detail["readProcessHandlesKnown"].is_boolean()){
                const bool known=detail["readProcessHandlesKnown"].get<bool>()&&natural("readProcessHandlesBefore",0xffffffffull)&&natural("readProcessHandlesAfter",0xffffffffull);
                result["readProcessHandlesKnown"]=known;
                if(known)for(const auto* key:{"readProcessHandlesBefore","readProcessHandlesAfter"})result[key]=detail[key];
            }
        }
        result["readReason"]="unknown";
        if(detail.contains("readReason")&&detail["readReason"].is_string()){
            const auto& reason=detail["readReason"].get_ref<const std::string&>();
            for(const auto* known:{"shared-steam-request-invalid","shared-steam-identity-unknown","shared-steam-request-expired",
                "shared-steam-document-read-unconfirmed","live-busy","document-plan-invalid","document-read-cancelled",
                "document-worker-unavailable","document-worker-cleanup-incomplete","document-worker-start-failed","document-worker-request-invalid",
                "document-worker-send-failed","document-worker-read-failed","document-listener-unavailable","document-worker-reply-invalid",
                "document-worker-deadline","document-worker-wait-failed","document-worker-failed","document-initialization-failed","document-cleanup-incomplete",
                "document-main-unavailable","document-source-unconfirmed","document-targets-invalid","document-target-limit",
                "document-transport-unavailable","document-receipt-invalid","document-identity-unconfirmed","document-replaced",
                "document-targets-changed","document-targets-refreshed","document-reply-invalid","document-evaluation-unconfirmed"})
                if(reason==known){result["readReason"]=reason;break;}
        }else if(detail.value("readOk",false))result["readReason"]="none";
        if(result.value("readOperation",std::string{})=="documents"&&result.value("readReason",std::string{})=="document-source-unconfirmed"&&
            result.contains("readOk")&&result["readOk"]==false&&result.contains("readComplete")&&result["readComplete"]==false&&
            detail.contains("targetSource")&&detail["targetSource"].is_object()){
            const auto& facts=detail["targetSource"];
            if(facts.size()==7&&facts.contains("schema")&&facts["schema"].is_number_integer()&&facts["schema"]==1&&
                facts.contains("phase")&&facts["phase"].is_string()&&(facts["phase"]=="discovery"||facts["phase"]=="final-targets")&&
                facts.contains("kind")&&facts["kind"].is_string()&&(facts["kind"]=="SP"||facts["kind"]=="MainMenu"||facts["kind"]=="QuickAccess")&&
                facts.contains("urlClass")&&facts["urlClass"].is_string()&&(facts["urlClass"]=="empty"||facts["urlClass"]=="blank-exact"||facts["urlClass"]=="blank-popup-exact"||facts["urlClass"]=="blank-variant"||facts["urlClass"]=="steam-variant"||facts["urlClass"]=="other")&&
                facts.contains("urlLength")&&facts["urlLength"].is_number_integer()&&facts["urlLength"]>=0&&facts["urlLength"]<=4097&&
                facts.contains("hasQuery")&&facts["hasQuery"].is_boolean()&&facts.contains("hasFragment")&&facts["hasFragment"].is_boolean())result["targetSource"]=facts;
        }
        for(const auto* key:{"documentCount","mainDocuments","menuDocuments","qamDocuments"})
            if(detail.contains(key)&&detail[key].is_number_integer()&&detail[key]>=0&&detail[key]<=12)result[key]=detail[key];
        if(detail.contains("elapsedMs")&&detail["elapsedMs"].is_number_unsigned())result["elapsedMs"]=(std::min)(detail["elapsedMs"].get<unsigned long long>(),3600000ull);
        if(detail.contains("steamPid")&&detail["steamPid"].is_number_unsigned()&&detail["steamPid"]<=0xffffffffull)result["steamPid"]=detail["steamPid"];
        if(detail.contains("steamBirth")&&detail["steamBirth"].is_string()){
            const auto& birth=detail["steamBirth"].get_ref<const std::string&>();
            if(!birth.empty()&&birth.size()<=20&&birth.find_first_not_of("0123456789")==std::string::npos)result["steamBirth"]=birth;
        }
        return result;
    }
    if(event=="connection-check"&&detail.value("scope",Json{})=="qam-open"){
        const auto source=detail.find("source");
        if(source!=detail.end()&&source->is_string()&&(*source=="open-or-legacy"||*source=="probe-result"))result["source"]=*source;
    }
    for(const auto* key:{"stageBudgetMs","stageRemainingMs","recoveryTrigger","generation","bindingRequested","preparationRequested","admissionRequested","connectionRequested","recoveryEpisode","nextRetryMs","scope","recoveryPhase","attempted","uncertain","elapsedMs","stage","ready","enabled","phase","reason","error","pid","revision","bindingRetry","bindingAttempts",
      "launchStage","launchMode","admissionRetry","admissionAttempts","admissionDetail","admissionElapsedMs","admissionNextRetryMs","admissionRecovery","deadlineMs","bootstrapStage","contextPid","contextCreated","contextGeneration","watching","ok","mutated","active","connected","steamKnown","steamRunning","steamDirectory","resourceDirectory","loaderPath","pluginPath","missingPath",
      "powerControlDirectory","directorySource","configuredOverride","logPath","createdSteamDebugMarker"})
      if(detail.contains(key)&&detail[key].is_primitive())result[key]=detail[key];
    if(event=="bootstrap-watch"&&detail.value("bootstrapStage",Json{})=="official-socket-close"){
        const auto facts=ymcc::deckyhealth::officialSocketFacts(detail.value("officialSocketClose",Json{}));
        if(!facts.is_null()){
            result["officialSocketClose"]=facts;
            const auto owner=officialLoaderOwnerRecord(detail.value("loaderOwnerAtReceipt",Json{}));
            if(!owner.is_null())result["loaderOwnerAtReceipt"]=owner;
        }
    }
    if(event=="bootstrap-watch"&&detail.value("bootstrapStage",Json{})=="mode-cache-miss"&&
       detail.value("scope",Json{})=="final-launch-admission"){
        const auto cause=detail.find("modeCacheCause");
        if(cause!=detail.end()&&cause->is_string()){
            const auto& text=cause->get_ref<const std::string&>();
            for(const auto* allowed:{"mode-refresh","connection-refresh","native-read-lock","session-changed","session-mismatch",
                    "read-cancelled","worker-wait-failed","no-demand","observer-stopping","no-observation"})
                if(text==allowed){result["modeCacheCause"]=text;break;}
        }
        const auto hold=detail.find("preparedBindingHold");
        if(hold!=detail.end()&&hold->is_string()){
            const auto& text=hold->get_ref<const std::string&>();
            for(const auto* allowed:{"held","not-eligible","binding-steam-scope-mismatch","binding-intent-unavailable","binding-window-closed",
                    "binding-proof-unverified","binding-context-unavailable","binding-owner-unavailable","binding-delegation-revoked","binding-preparation-changed",
                    "binding-owner-inactive","binding-owner-replaced","binding-child-unavailable","binding-owner-check-failed"})
                if(text==allowed){result["preparedBindingHold"]=text;break;}
        }
        for(const auto* key:{"modeCacheOriginLine","steamPid"})
            if(detail.contains(key)&&detail[key].is_number_unsigned()&&detail[key]<=0xffffffffull)result[key]=detail[key];
        if(detail.contains("modeCacheSequence")&&detail["modeCacheSequence"].is_number_unsigned())
            result["modeCacheSequence"]=detail["modeCacheSequence"];
        if(detail.contains("steamBirth")&&detail["steamBirth"].is_string()){
            const auto& birth=detail["steamBirth"].get_ref<const std::string&>();
            if(!birth.empty()&&birth.size()<=20&&birth.find_first_not_of("0123456789")==std::string::npos)result["steamBirth"]=birth;
        }
    }
    if(event=="lossless-launch-receipt"||event=="lossless-scale-receipt"||event=="lossless-reset-receipt"){
        for(const auto* key:{"started","reused","invoked","stopInvoked","restarted","cancelled","pending","paused","readable","reapply","refreshWindow","refreshAttempted","windowRefreshed","refreshHelperCreated","refreshHelperDestroyed","refreshLeftGame","refreshReturnRequested","refreshReturned","refreshCancelled","refreshApiAccepted","refreshExperiment","refreshFrameAccepted","refreshRedrawAccepted","refreshTopAccepted","refreshForegroundAccepted","failureResetRequested","failureResetAttempted","failureResetTerminated","failureResetClosed"})
            if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        if(detail.contains("processCreated")&&detail["processCreated"].is_string()){
            const auto text=detail["processCreated"].get<std::string>();
            if(!text.empty()&&text.size()<=20&&text.find_first_not_of("0123456789")==std::string::npos)result["processCreated"]=text;
        }
        if(detail.contains("failureResetTrigger")&&detail["failureResetTrigger"].is_string()&&detail["failureResetTrigger"]=="scaling-ipc-timeout")result["failureResetTrigger"]="scaling-ipc-timeout";
        if(detail.contains("failureResetPolicy")&&detail["failureResetPolicy"].is_string()&&detail["failureResetPolicy"]=="ipc-timeout-only")result["failureResetPolicy"]="ipc-timeout-only";
        if(detail.contains("retryAttempt")&&detail["retryAttempt"].is_number_unsigned()&&detail["retryAttempt"].get<unsigned>()<=1)result["retryAttempt"]=detail["retryAttempt"];
        if(detail.contains("refreshMethod")&&detail["refreshMethod"].is_number_unsigned()&&detail["refreshMethod"].get<unsigned>()<=7)result["refreshMethod"]=detail["refreshMethod"];
        if(detail.contains("refreshMethodName")&&detail["refreshMethodName"].is_string()){
            const auto name=detail["refreshMethodName"].get<std::string>();
            if(name=="redraw"||name=="frame-changed"||name=="foreground-request"||name=="normal-helper")result["refreshMethodName"]=name;
        }
        if(detail.contains("win32Error")&&detail["win32Error"].is_number_unsigned())result["win32Error"]=detail["win32Error"];
        if(detail.contains("refreshElapsedMs")&&detail["refreshElapsedMs"].is_number_unsigned())result["refreshElapsedMs"]=(std::min)(detail["refreshElapsedMs"].get<unsigned long long>(),12000ull);
        if(detail.contains("failureResetError")&&detail["failureResetError"].is_number_unsigned())result["failureResetError"]=detail["failureResetError"];
        for(const auto* key:{"failureResetReason","refreshReason"})if(detail.contains(key)&&detail[key].is_string()){
            const auto reason=detail[key].get<std::string>();
            if(reason.size()<=64&&reason.find_first_not_of("abcdefghijklmnopqrstuvwxyz-")==std::string::npos)result[key]=reason;
        }
    }
    if(event=="mirror-peer"&&detail.contains("peerClose")&&detail["peerClose"].is_object()){
        const auto& facts=detail["peerClose"];Json clean=Json::object();
        if(facts.contains("closeReason")&&facts["closeReason"].is_string()){
            const auto& reason=facts["closeReason"].get_ref<const std::string&>();
            for(const auto* known:{"owner-stop","reply-owner-retired","send-error","write-progress-timeout","frame-timeout","handshake-timeout",
                "wait-error","listener-events-error","peer-events-setup-error","output-limit","socket-events-error","input-limit","remote-eof","receive-error",
                "handshake-error","frame-error","remote-close","request-json-error","request-identity-error","request-command-error","request-callback-error","socket-close"})
                if(reason==known){clean["closeReason"]=reason;break;}
        }
        for(const auto* key:{"socketError","websocketCloseCode","connectionAgeMs","receiveIdleMs","pendingOutputBytes","bufferedInputBytes"})
            if(facts.contains(key)&&facts[key].is_number_integer()&&facts[key]>=0&&facts[key]<=9007199254740991ull)clean[key]=facts[key];
        if(facts.contains("websocketCloseCodeKnown")&&facts["websocketCloseCodeKnown"].is_boolean())clean["websocketCloseCodeKnown"]=facts["websocketCloseCodeKnown"];
        if(!clean.empty())result["peerClose"]=std::move(clean);
    }
    if(event=="prewarm"){
        // Fixed labels from this call's original mayPrepare predicate only.
        // Missing/none is not continued owner or file-health proof.
        if(detail.contains("preparationBudgetMs")&&detail["preparationBudgetMs"].is_number_unsigned()&&detail["preparationBudgetMs"]==2500u)
            result["preparationBudgetMs"]=detail["preparationBudgetMs"];
        if(detail.contains("preparationFirstRefusal")&&detail["preparationFirstRefusal"].is_string()){
            const auto& kind=detail["preparationFirstRefusal"].get_ref<const std::string&>();
            for(const auto* known:{"none","owner-not-current","exit-requested","power-not-ready","budget-expired"})
                if(kind==known){result["preparationFirstRefusal"]=kind;break;}
        }
    }
    if(event=="prepare"){
        // Exact framing/ownership facts. No paths, pipe payload or credentials.
        for(const auto* key:{"resourceWorkerDestroyed","resourceWorkerReplyReceived","resourceWorkerChildExitKnown","resourceWorkerChildCpuKnown"})
            if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        for(const auto* key:{"resourceWorkerReadError","resourceWorkerChildExitCode","resourceWorkerStartError","resourceWorkerSendError","resourceWorkerWaitError"})
            if(detail.contains(key)&&detail[key].is_number_unsigned()&&detail[key]<=4294967295ull)result[key]=detail[key];
        if(detail.contains("resourceWorkerElapsedMs")&&detail["resourceWorkerElapsedMs"].is_number_unsigned())
            result["resourceWorkerElapsedMs"]=(std::min)(detail["resourceWorkerElapsedMs"].get<unsigned long long>(),3600000ull);
        if(result.value("resourceWorkerChildCpuKnown",false)&&detail.contains("resourceWorkerChildCpuMs")&&detail["resourceWorkerChildCpuMs"].is_number_unsigned())
            result["resourceWorkerChildCpuMs"]=(std::min)(detail["resourceWorkerChildCpuMs"].get<unsigned long long>(),3600000ull);
        if(detail.contains("resourceWorkerProgressCount")&&detail["resourceWorkerProgressCount"].is_number_unsigned()&&detail["resourceWorkerProgressCount"]<=4u)
            result["resourceWorkerProgressCount"]=detail["resourceWorkerProgressCount"];
        if(detail.contains("resourceWorkerChildElapsedMs")&&detail["resourceWorkerChildElapsedMs"].is_number_unsigned())
            result["resourceWorkerChildElapsedMs"]=(std::min)(detail["resourceWorkerChildElapsedMs"].get<unsigned long long>(),3600000ull);
        if(detail.contains("resourceWorkerChildStage")&&detail["resourceWorkerChildStage"].is_string()){
            const auto& stage=detail["resourceWorkerChildStage"].get_ref<const std::string&>();
            if(stage=="none"||stage=="receipt"||stage=="inspect"||stage=="cache"||stage=="reinspect")result["resourceWorkerChildStage"]=stage;
        }
        if(detail.contains("resourceWorkerReadKind")&&detail["resourceWorkerReadKind"].is_string()){
            const auto& kind=detail["resourceWorkerReadKind"].get_ref<const std::string&>();
            if(kind=="attach"||kind=="ended"||kind=="failed")result["resourceWorkerReadKind"]=kind;
        }
        if(detail.contains("resourceWorkerStage")&&detail["resourceWorkerStage"].is_string()){
            const auto& stage=detail["resourceWorkerStage"].get_ref<const std::string&>();
            if(stage=="construct"||stage=="prepare"||stage=="start"||stage=="send"||stage=="attach"||stage=="read"||stage=="reply")result["resourceWorkerStage"]=stage;
        }
    }
    if(event=="lossless-action-finished")for(const auto* key:{"completed","applied","pending"})
        if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
    if(event=="mirror-snapshot-request"||event=="snapshot-demand"||event=="snapshot-read-start"||event=="snapshot-read-finished") {
        for(const auto* key:{"subscribe","readId","demandEpoch","discarded","powerMs","powerSourceMs","scheduleMs","performanceMs","topologyMs","gyroMs","framesMs","touchpadsMs","globalMs","monitorMs","inputMs"})
            if(detail.contains(key)&&detail[key].is_primitive())result[key]=detail[key];
    }
    if(event=="mirror-snapshot-request"&&detail.contains("clientStartup")){
        const auto facts=clientStartupRecord(detail["clientStartup"]);if(!facts.is_null())result["clientStartup"]=facts;
    }
    if(event=="snapshot-read-finished"){
        snapshotReadFailureFacts(result,detail);performanceReadTimings(result,detail);
        if(const auto* profile=snapshotProfileReadTimings(detail))result["profileReadMs"]=*profile;
    }
    if((event=="bootstrap"||event=="bootstrap-watch")&&detail.contains("sessionCheck")&&detail["sessionCheck"].is_string()){
        const auto check=detail["sessionCheck"].get<std::string>();
        if(check=="steam-process-or-birth"||check=="steam-user-or-session"||check=="steam-process-exited"||
           check=="steam-windows-session"||check=="steam-account"||check=="steam-root"||check=="steam-registered-pid")result["sessionCheck"]=check;
    }
    // An acknowledged action does not prove that the new sidebar is usable.
    if(event=="connection-recovery"&&detail.value("reason",Json{})=="decky-recovery-unconfirmed"&&
        (detail.value("stage",Json{})=="rebuild-ui-document"||detail.value("stage",Json{})=="reopen-bigpicture")&&
        detail.value("ok",Json{})==true&&detail.value("mutated",Json{})==true&&detail.value("uncertain",Json{})==false)
        result["reason"]="decky-recovery-action-acknowledged";
    if(event=="connection-recovery"&&detail.value("scope",std::string{})=="global-connection"){
        if(detail.contains("bindingReused")&&detail["bindingReused"].is_boolean())result["bindingReused"]=detail["bindingReused"];
        if(detail.contains("runtimeReason")&&detail["runtimeReason"].is_string()&&detail["runtimeReason"].get_ref<const std::string&>().size()<=128)result["runtimeReason"]=detail["runtimeReason"];
        if(detail.contains("steamPid")&&detail["steamPid"].is_number_unsigned()&&detail["steamPid"]<=0xffffffffull)result["steamPid"]=detail["steamPid"];
        if(detail.contains("steamBirth")&&detail["steamBirth"].is_string()){
            const auto& birth=detail["steamBirth"].get_ref<const std::string&>();
            if(!birth.empty()&&birth.size()<=20&&birth.find_first_not_of("0123456789")==std::string::npos)result["steamBirth"]=birth;
        }
    }
    if(event=="steam-mode-read") {
        const auto cancel=detail.find("modeReadCancelCheck"),startPower=detail.find("modeReadStartPowerGeneration"),
            startSerial=detail.find("modeReadStartSuspendSerial");
        if(detail.value("reason",Json{})=="shared-mode-read-cancelled"&&cancel!=detail.end()&&cancel->is_string()&&
            startPower!=detail.end()&&startPower->is_number_unsigned()&&*startPower!=0u&&
            startSerial!=detail.end()&&startSerial->is_number_unsigned()){
            const auto& label=cancel->get_ref<const std::string&>();
            if(label=="observer-fence"||label=="owner-exit"||label=="power-scope"||label=="identity-epoch"||label=="check-error"){
                result["modeReadCancelCheck"]=*cancel;
                result["modeReadStartPowerGeneration"]=*startPower;
                result["modeReadStartSuspendSerial"]=*startSerial;
            }
        }
        const auto busyKind=detail.find("modeReadBusyKind");
        if(busyKind!=detail.end()&&busyKind->is_string()){
            const auto& kind=busyKind->get_ref<const std::string&>();
            const auto why=detail.find("reason");
            const bool explicitBusy=why!=detail.end()&&*why=="live-busy"&&
                (kind=="native-transport-lock"||kind=="steam-renderer-busy");
            if(explicitBusy||kind=="native-read-lock"||kind=="shared-live-channel"||kind=="unknown")result["modeReadBusyKind"]=*busyKind;
            if(explicitBusy&&kind=="native-transport-lock"){
                const auto holder=detail.find("modeTransportHolderAtBusy");
                const auto wait=detail.find("modeTransportWaitResult");
                if(wait!=detail.end()&&*wait=="busy"&&holder!=detail.end()&&holder->is_string()){
                    const auto& tag=holder->get_ref<const std::string&>();
                    if(tag=="unknown"||tag=="mode-read"||tag=="menu"||tag=="sleep"||tag=="decky"||tag=="other-read"||
                       tag=="other-action"||tag=="documents-native"||tag=="documents-worker"){
                        result["modeTransportHolderAtBusy"]=*holder;
                        const auto age=detail.find("modeTransportObservedHeldMs");
                        if(tag!="unknown"&&age!=detail.end()&&age->is_number_unsigned()&&*age<=60000u){
                            result["modeTransportObservedHeldMs"]=*age;
                            const auto tick=detail.find("modeTransportHolderSampleTick");
                            if(tick!=detail.end()&&tick->is_number_unsigned()&&*tick!=0u&&*tick>=*age&&
                               tick->get<std::uint64_t>()<=((std::numeric_limits<std::uint64_t>::max)()>>8))
                                result["modeTransportHolderSampleTick"]=*tick;
                        }
                        if(tag=="sleep"){
                            const auto op=detail.find("modeTransportSleepOperationAtBusy");
                            if(op!=detail.end()&&op->is_string()&&
                               (*op=="sleep.session.inspect"||*op=="sleep.inspect"||
                                *op=="sleep.reconcile"||*op=="sleep.input-resume"))
                                result["modeTransportSleepOperationAtBusy"]=*op;
                        }
                    }
                }
            }
        }
        const auto waitResult=detail.find("modeTransportWaitResult"),waitMs=detail.find("modeTransportWaitMs");
        if(waitResult!=detail.end()&&(*waitResult=="acquired"||*waitResult=="busy"||*waitResult=="cancelled")&&
           waitMs!=detail.end()&&waitMs->is_number_unsigned()&&*waitMs<=60000u){
            result["modeTransportWaitResult"]=*waitResult;
            result["modeTransportWaitMs"]=*waitMs;
            const auto extraSlices=detail.find("modeTransportSleepInspectExtraSlices");
            if(extraSlices!=detail.end()&&extraSlices->is_number_unsigned()&&*extraSlices>=1u&&*extraSlices<=2u)
                result["modeTransportSleepInspectExtraSlices"]=*extraSlices;
        }
        const auto backendState=detail.find("deckyBackendState");
        if(backendState!=detail.end()&&backendState->is_string()&&deckyfrontend::backendState(backendState->get_ref<const std::string&>()))result["deckyBackendState"]=*backendState;
        const auto clientState=detail.find("deckyClientState");
        if(clientState!=detail.end()&&clientState->is_string()&&deckyfrontend::clientState(clientState->get_ref<const std::string&>()))result["deckyClientState"]=*clientState;
        for(const auto* key:{"sessionPidPresent","sessionBirthPresent","sessionRootPresent","registryAccountPresent",
            "modeKnown","modeRunning","modeIdentityConfirmed","uiAccountPresent","uiAccountWatchKnown","uiAccountWatchAvailable",
            "deckyBackendKnown","deckyBackendConnected","deckyHooksKnown","deckyHooksInstalled","deckyClientKnown","deckyClientConnected","deckyClientCheckingKnown","deckyClientChecking","deckyClientVerifiedKnown","deckyClientVerified",
            "uiRegistryAccountComparable","uiRegistryAccountMatches","uiAccountSelected","registryMismatchIgnored","registryReadKnown","registryReadAccountPresent",
            "registryReadMatchesSession","registryReadMatchesUi","registryReadPidKnown","registryReadPidPresent","registryReadPidMatchesSession","registryReadFromSharedSnapshot","registrySnapshotAvailable","registrySnapshotMatchesProcess"})
            if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        if(detail.contains("steamPid")&&detail["steamPid"].is_number_unsigned()&&detail["steamPid"].get<unsigned long long>()<=0xffffffffull)result["steamPid"]=detail["steamPid"];
        if(detail.contains("steamBirth")&&detail["steamBirth"].is_string()){
            const auto& birth=detail["steamBirth"].get_ref<const std::string&>();
            if(!birth.empty()&&birth.size()<=20&&birth.find_first_not_of("0123456789")==std::string::npos)result["steamBirth"]=birth;
        }
        if(detail.contains("proofStage")&&detail["proofStage"].is_string()){
            const auto& stage=detail["proofStage"].get_ref<const std::string&>();
            for(const auto* allowed:{"session-pid-missing","session-birth-missing","session-root-missing","mode-unconfirmed",
                "mode-identity-mismatch","desktop","account-mismatch","registry-account","ui-account-missing",
                "ui-account-watch-unconfirmed","verified-steam-ui"})if(stage==allowed){result["proofStage"]=stage;break;}
        }
    }
    if(event=="steam-mode-read")for(const auto* key:{"registryReadError","registryReadPidError"})
        if(detail.contains(key)&&detail[key].is_number_unsigned()&&detail[key].get<unsigned long long>()<=0xffffffffull)result[key]=detail[key];
    if(event=="steam-session-check") {
        result={{"event",event}};
        for(const auto* key:{"pidPresent","birthPresent","accountPresent","rootPresent","uiAccountConfirmed"})
            if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        for(const auto* key:{"steamPid","error"})
            if(detail.contains(key)&&detail[key].is_number_unsigned()&&detail[key].get<unsigned long long>()<=0xffffffffull)result[key]=detail[key];
        if(detail.contains("steamBirth")&&detail["steamBirth"].is_string()){
            const auto& birth=detail["steamBirth"].get_ref<const std::string&>();
            if(!birth.empty()&&birth.size()<=20&&birth.find_first_not_of("0123456789")==std::string::npos)result["steamBirth"]=birth;
        }
        if(detail.contains("accountSource")&&detail["accountSource"]=="verified-steam-ui")result["accountSource"]="verified-steam-ui";
        if(detail.contains("stage")&&detail["stage"].is_string()){
            const auto& stage=detail["stage"].get_ref<const std::string&>();
            if(stage=="binding-identity-verified"||stage=="launch-identity-incomplete"||stage=="binding-identity-incomplete"||stage=="windows-session-query"||stage=="session-changed"||stage=="observer-session"||stage=="observer-presence"||stage=="observer-initial")result["stage"]=stage;
        }
    }
    if(event=="wallpaper-inventory"||event=="wallpaper-prepare") {
        result={{"event",event}};
        for(const auto* key:{"elapsedMs","targetCount","shortcutCount","verifiedCount","shortcutTargets","shortcutPending","slotCount","retryDelayMs"})if(detail.contains(key)&&detail[key].is_number_unsigned())result[key]=(std::min)(detail[key].get<unsigned long long>(),3600000ull);
        if(event=="wallpaper-inventory"&&detail.contains("shortcutRead")){const auto facts=shortcutReadRecord(detail["shortcutRead"]);if(facts.is_object())result["shortcutRead"]=facts;}
        if(event=="wallpaper-inventory"){
            // Ordered wire bits from InventoryOrigin in sharedGameMedia.ts. Reject unknown combinations.
            constexpr const char* names[]={"library-notice","page-hint","manual","enabled","power-recovery",
                "steam-process","steam-account","steam-document","steam-mode","steam-revalidated"};
            const auto mask=detail.find("originMask");
            if(mask!=detail.end()&&mask->is_number_integer()){
                const auto bits=mask->get<unsigned long long>();
                constexpr auto count=sizeof(names)/sizeof(names[0]);
                if(bits&&bits<(1ull<<count)){
                    auto origins=Json::array();
                    for(size_t i=0;i<count;++i)if(bits&(1ull<<i))origins.push_back(names[i]);
                    result["origins"]=std::move(origins);
                }
            }
        }
        for(const auto* key:{"ok","accountStable","shortcutsAvailable","staticReady","dynamicReady"})if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        const auto allow=[&](const char* key,std::initializer_list<const char*> choices){if(!detail.contains(key)||!detail[key].is_string())return;const auto& value=detail[key].get_ref<const std::string&>();for(const auto* choice:choices)if(value==choice){result[key]=value;break;}};
        allow("stage",{"started","finished","superseded","failed"});
        allow("reason",{"ready","waiting-steam-account","shortcut-inventory-unavailable","account-changed","inventory-read-failed","identity-unconfirmed","superseded","network-failed","prepare-failed","steam-path-unavailable","shortcut-file-missing","shortcut-file-status-failed","shortcut-file-not-regular","shortcut-file-too-large","shortcut-file-read-failed","shortcut-file-changed","shortcut-vdf-invalid"});
    }
    if(event=="bootstrap-watch" &&
       ((detail.value("scope",Json{})=="loader-environment"&&detail.value("bootstrapStage",Json{})=="loader-temp-prepared")||
        (detail.value("scope",Json{})=="loader-exit"&&detail.value("bootstrapStage",Json{})=="loader-exited"))){
        // Consumer lifecycle facts only. Never export TEMP itself, dialog bodies,
        // inherited environment blocks, bootstrap credentials or request contents.
        if(detail.contains("tempPathUnits")&&detail["tempPathUnits"].is_number_unsigned()&&detail["tempPathUnits"].get<unsigned long long>()<=32767ull)result["tempPathUnits"]=detail["tempPathUnits"];
        if(detail.contains("tempShortRoot")&&detail["tempShortRoot"].is_boolean())result["tempShortRoot"]=detail["tempShortRoot"];
        if(detail.contains("loaderUptimeMs")&&detail["loaderUptimeMs"].is_number_unsigned())result["loaderUptimeMs"]=(std::min)(detail["loaderUptimeMs"].get<unsigned long long>(),604800000ull);
        if(detail.contains("crashFailures")&&detail["crashFailures"].is_number_unsigned()&&detail["crashFailures"].get<unsigned long long>()<=5ull)result["crashFailures"]=detail["crashFailures"];
    }
    if(event=="bootstrap-watch"&&detail.value("scope",Json{})=="loader-stop"&&
       (detail.value("bootstrapStage",Json{})=="loader-stop-started"||detail.value("bootstrapStage",Json{})=="loader-stop-finished")){
        if(detail.contains("loaderStopCause")&&detail["loaderStopCause"].is_string()){
            const auto& cause=detail["loaderStopCause"].get_ref<const std::string&>();
            for(const auto* known:{"job-create-failed","launch-failed","ownership-failed","environment-failed","runtime-stop","loader-exited",
                "connection-recycle","connection-rebuild","connection-reopen","explicit-off","steam-unknown","steam-absent","wait-failed"})
                if(cause==known){result["loaderStopCause"]=cause;break;}
        }
        if(detail.contains("loaderBirth100ns")&&detail["loaderBirth100ns"].is_string()){
            const auto& birth=detail["loaderBirth100ns"].get_ref<const std::string&>();
            if(!birth.empty()&&birth.size()<=20&&birth.find_first_not_of("0123456789")==std::string::npos)result["loaderBirth100ns"]=birth;
        }
        if(detail.value("bootstrapStage",Json{})=="loader-stop-finished"){
            for(const auto* key:{"loaderStopRootExited","loaderStopExitKnown","loaderStopJobEmptyConfirmed","loaderStopBindingDestroyed"})
                if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
            for(const auto* key:{"loaderStopWait","loaderStopWaitError","loaderStopExitCode"})
                if(detail.contains(key)&&detail[key].is_number_unsigned()&&detail[key]<=0xffffffffull&&
                   (std::strcmp(key,"loaderStopExitCode")!=0||result.value("loaderStopExitKnown",false)))result[key]=detail[key];
        }
    }
    if(event=="managed-lifecycle"){
        // One fixed inspect refusal, never identities, versions or raw history.
        if(detail.contains("managedInspectFailure")&&detail["managedInspectFailure"].is_string()){
            const auto& stage=detail["managedInspectFailure"].get_ref<const std::string&>();
            for(const auto* known:{"ledger-missing","ledger-invalid","reservation-pending","document-changed",
                "steam-session-changed","frontend-changed","loader-changed","unconfirmed"})
                if(stage==known){result["managedInspectFailure"]=stage;break;}
        }
        // Framing facts only: no raw frame, payload, pipe handle or credentials.
        if(detail.contains("cleanupWorkerReadError")&&detail["cleanupWorkerReadError"].is_number_unsigned()&&
           detail["cleanupWorkerReadError"]<=4294967295ull)result["cleanupWorkerReadError"]=detail["cleanupWorkerReadError"];
        if(detail.contains("cleanupWorkerReadKind")&&(detail["cleanupWorkerReadKind"]=="ended"||detail["cleanupWorkerReadKind"]=="failed"))
            result["cleanupWorkerReadKind"]=detail["cleanupWorkerReadKind"];
        for(const auto* key:{"cleanupWorkerReplyReceived","readbackPending"})
            if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
    }
    if(event=="steam-mode-signal"){
        // The mode callback already reports both versions. Keep only bounded
        // numeric release facts so old-document refusal is diagnosable without
        // recording a raw residue object, endpoint, token or JS source.
        for(const auto* key:{"observedLoaderVersion","installedLoaderVersion"}){
            if(!detail.contains(key)||!detail[key].is_string())continue;
            const auto& value=detail[key].get_ref<const std::string&>();
            if(value.size()<6||value.size()>32||value.front()!='v')continue;
            unsigned dots=0,digits=0;bool valid=true;
            for(size_t at=1;at<value.size();++at){
                const char ch=value[at];
                if(ch=='.'){if(!digits||++dots>2){valid=false;break;}digits=0;}
                else if(ch>='0'&&ch<='9')++digits;
                else {valid=false;break;}
            }
            if(valid&&dots==2&&digits)result[key]=value;
        }
    }
    if(event=="plugin-build"&&detail.contains("buildId")&&validBuildId(detail["buildId"]))result["buildId"]=detail["buildId"];
    if(event=="prepare"||event=="environment") {
        for(const auto* key:{"loaderExpectedSha256","loaderObservedSha256"})
            if(detail.contains(key)&&validBuildId(detail[key]))result[key]=detail[key];
        for(const auto* key:{"resourceCacheAvailable","resourceRepairAttempted","resourceWorkerDestroyed","resourceMaintenanceDeferred"})if(detail.contains(key)&&detail[key].is_boolean())result[key]=detail[key];
        if(detail.contains("resourcesRepaired")&&detail["resourcesRepaired"].is_number_unsigned()&&detail["resourcesRepaired"]<=7)result["resourcesRepaired"]=detail["resourcesRepaired"];
        if(detail.contains("resourceRepairReason")&&detail["resourceRepairReason"].is_string()){const auto reason=detail["resourceRepairReason"].get<std::string>();if(reason.size()<=96&&reason.starts_with("resource-cache-")&&reason.find_first_not_of("abcdefghijklmnopqrstuvwxyz-")==std::string::npos)result["resourceRepairReason"]=reason;}
        if(detail.contains("managedPackageSource")&&detail["managedPackageSource"].is_string()){const auto source=detail["managedPackageSource"].get<std::string>();if(source=="receipt"||source=="package"||source=="legacy")result["managedPackageSource"]=source;}
        if(detail.contains("managedPackageVerified")&&detail["managedPackageVerified"].is_boolean())result["managedPackageVerified"]=detail["managedPackageVerified"];
        if(detail.contains("loaderHashMatched")&&detail["loaderHashMatched"].is_boolean())result["loaderHashMatched"]=detail["loaderHashMatched"];
        if(detail.value("loaderOrigin",Json())=="official-release")result["loaderOrigin"]="official-release";
        if(detail.contains("loaderVersion")&&detail["loaderVersion"].is_string()){const auto& v=detail["loaderVersion"].get_ref<const std::string&>();if(!v.empty()&&v.size()<=32&&v.find_first_not_of("v0123456789.")==std::string::npos)result["loaderVersion"]=v;}
        if(detail.contains("loaderModified")&&detail["loaderModified"].is_boolean())result["loaderModified"]=detail["loaderModified"];
        if(detail.contains("loaderConsumerContract")&&detail["loaderConsumerContract"].is_number_integer()&&detail["loaderConsumerContract"]>=1&&detail["loaderConsumerContract"]<=1000)result["loaderConsumerContract"]=detail["loaderConsumerContract"];
        if(detail.contains("loaderPolicyVersion")&&detail["loaderPolicyVersion"].is_number_integer()&&detail["loaderPolicyVersion"]>=0&&detail["loaderPolicyVersion"]<=1000)
            result["loaderPolicyVersion"]=detail["loaderPolicyVersion"];
    }
    // CSS callbacks already provide these bounded facts, but the generic
    // allowlist previously dropped them, making cleanup look verified by error=0.
    if(event=="steam-css-use" && (detail.value("stage",Json())=="idle-helper-complete" || detail.value("stage",Json())=="automatic-helper-failed")) {
        const auto timing=detail.find("helperTiming");
        if(timing!=detail.end() && timing->is_object()) {
            Json bounded=Json::object();
            for(const char* key:{"entryToRefreshMs","firstControlMs","firstMutexWaitMs","firstCssRpcMs","firstCssRpcCount","firstDomMs","entryToReadyMs"}) {
                const auto value=timing->find(key);
                if(value==timing->end() || !value->is_number_unsigned() || *value>180000u) break;
                bounded[key]=*value;
            }
            if(bounded.size()==7 && bounded["firstCssRpcCount"]<=128u) {
                if(const auto source=timing->find("firstControlParts");source!=timing->end() && source->is_object()) {
                    Json parts=Json::object();
                    auto sum=bounded["firstMutexWaitMs"].get<unsigned>()+bounded["firstCssRpcMs"].get<unsigned>();
                    for(const char* key:{"statusIdentityMs","cssCandidatesMs","cssRpcOwnerMs","themeStateMs"}) {
                        const auto part=source->find(key);
                        if(part==source->end() || !part->is_number_unsigned() || *part>180000u) break;
                        sum+=part->get<unsigned>(); parts[key]=*part;
                    }
                    if(parts.size()==4 && sum<=bounded["firstControlMs"].get<unsigned>()) bounded["firstControlParts"]=std::move(parts);
                }
                result["helperTiming"]=std::move(bounded);
            }
        }
        const auto trace=detail.find("renderTrace");
        if(trace!=detail.end() && trace->is_object() && trace->contains("attempts") && trace->contains("correctionAttempt")) {
            const auto& attempts=(*trace)["attempts"];
            const auto& correction=(*trace)["correctionAttempt"];
            if(attempts.is_array() && !attempts.empty() && attempts.size()<=5 && correction.is_number_unsigned() && correction<=attempts.size()) {
                auto rows=Json::array();
                for(const auto& row:attempts) {
                    const auto index=rows.size()+1;
                    if(!row.is_object() || !row.contains("attempt") || !row["attempt"].is_number_unsigned() || row["attempt"]!=index ||
                       !row.contains("waitMs") || !row["waitMs"].is_number_unsigned() || row["waitMs"]>180000u ||
                       !row.contains("outcome") || !row["outcome"].is_number_unsigned() || row["outcome"]<1u || row["outcome"]>6u ||
                       (correction==index && row["outcome"]!=4u && row["outcome"]!=6u)) break;
                    rows.push_back({{"attempt",index},{"waitMs",row["waitMs"]},{"outcome",row["outcome"]}});
                }
                if(rows.size()==attempts.size())result["renderTrace"]={{"attempts",std::move(rows)},{"correctionAttempt",correction}};
            }
        }
        const auto correctionTiming=detail.find("correctionTiming");
        if(result.contains("renderTrace") && result["renderTrace"]["correctionAttempt"]!=0u &&
           correctionTiming!=detail.end() && correctionTiming->is_object()) {
            Json bounded=Json::object();
            auto take=[&](const char* key,unsigned maximum) {
                const auto value=correctionTiming->find(key);
                if(value==correctionTiming->end() || !value->is_number_unsigned() || *value>maximum) return false;
                bounded[key]=*value;
                return true;
            };
            if(take("startMs",180000) && take("endMs",180000) && take("outcome",5) &&
               take("setAttempts",7) && take("setAcknowledged",7)) {
                const auto start=bounded["startMs"].get<unsigned>(),end=bounded["endMs"].get<unsigned>(),
                    outcome=bounded["outcome"].get<unsigned>(),sent=bounded["setAttempts"].get<unsigned>(),
                    ack=bounded["setAcknowledged"].get<unsigned>();
                if(start<=end && outcome && ack<=sent && (!(outcome==2 || outcome==3) || !sent) &&
                   (outcome<4 || ack==sent) &&
                   (!sent || (take("firstSetStartMs",end) && bounded["firstSetStartMs"]>=start)) &&
                   (!ack || (take("lastSetAckMs",end) && bounded["lastSetAckMs"]>=bounded["firstSetStartMs"]))) {
                    if(const auto source=correctionTiming->find("setReasons");source!=correctionTiming->end() && source->is_object()) {
                        Json reasons=Json::object(); unsigned sum=0;
                        for(const auto* key:{"flagOnly","receiptOnly","both"}) {
                            const auto count=source->find(key);
                            if(count==source->end() || !count->is_number_unsigned() || *count>sent) break;
                            sum+=count->get<unsigned>(); reasons[key]=*count;
                        }
                        if(reasons.size()==3 && sum==sent) bounded["setReasons"]=std::move(reasons);
                    }
                    result["correctionTiming"]=std::move(bounded);
                }
            }
        }
    }
    if(event=="steam-css-resume"&&detail.value("reason",Json{})=="public-ready-scope") {
        const auto suspend=detail.find("suspendSerial"),notification=detail.find("notificationSequence");
        if(suspend!=detail.end()&&notification!=detail.end()&&suspend->is_number_unsigned()&&notification->is_number_unsigned()&&
           (*suspend!=0u||*notification!=0u)) {
            result["suspendSerial"]=*suspend;
            result["notificationSequence"]=*notification;
        }
    }
    if (event == "steam-css-guard" && detail.value("reason", Json{}) == "bigpicture-entered") {
        const auto timing = detail.find("modeHandoff");
        if (timing != detail.end()) {
            auto bounded = cssModeHandoffRecord(*timing);
            if (bounded.is_object()) result["modeHandoff"] = std::move(bounded);
        }
    }
    if(event=="steam-css-use"||event=="steam-css-guard"||event=="steam-css-resume"||event=="steam-mode-signal") {
        for(const auto* key:{"policyVersion","steamPid","steamBirth","loaderPid","linked","attempts","verified","handleFallback",
            "remaining","unverified","secondaryCheckPending","minimumHoldPending","secondaryDelayMs","minimumHoldMs","helperExitKnown","helperExitCode","helperResultKnown","helperPhase","helperReason","helperFailureCode","helperHResult","backendReady","configuredMatchesBackend","domConfirmed","minimumWindowConfirmed","backendExitConfirmed","backendProcessCount","editorProtected","editorStateUnknown"})
            if(detail.contains(key)&&detail[key].is_primitive())result[key]=detail[key];
        if(detail.contains("loaders")&&detail["loaders"].is_array()) {
            auto rows=Json::array();
            for(const auto& row:detail["loaders"]) {
                if(rows.size()>=16)break;
                if(row.is_object()&&row.contains("pid")&&row["pid"].is_number_integer()&&row.contains("birth")&&row["birth"].is_string()&&row["birth"].get_ref<const std::string&>().size()<=20)
                    rows.push_back({{"pid",row["pid"]},{"birth",row["birth"]}});
            }
            result["loaders"]=rows;result["loaderIdentitiesTruncated"]=detail["loaders"].size()>rows.size();
        }
    }
    return result;
}
inline Json failureRecord(const Json& detail,const ymcc::steamfailure::FailureContext& context) {
    // Keep only bounded control facts from the native boundary. The common
    // projector alone selects remote error classes/codes, never free text.
    Json row={{"event","steam-recovery-trace"}};
    for(const auto* key:{"reason","runtimeReason","recoveryTrigger"}){
        const auto it=detail.find(key);
        if(detail.is_object()&&it!=detail.end()&&it->is_string()){
            const auto& value=it->get_ref<const std::string&>();
            if(value.size()<=128&&value.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-_")==std::string::npos)row[key]=value;
        }
    }
    for(const auto* key:{"stageBudgetMs","stageRemainingMs"}){
        const auto it=detail.find(key);if(detail.is_object()&&it!=detail.end()&&it->is_number_unsigned()&&*it<=3600000u)row[key]=*it;
    }
    row.update(ymcc::steamfailure::project(detail,context));
    try{if(context.phase==ymcc::steamfailure::Phase::Finished&&
        (context.operation==ymcc::steamfailure::Operation::RebuildDocument||context.operation==ymcc::steamfailure::Operation::ReopenBigPicture)){
        const auto socket=detail.find("cleanupPreflightBackendState");
        if(socket!=detail.end()&&socket->is_string()){
            const auto& label=socket->get_ref<const std::string&>();
            if(label=="unconfirmed"||deckyfrontend::backendState(label))row["cleanupPreflightBackendState"]=*socket;
        }
    }}catch(...){}
    // The parent retains its original action/Steam/power stamp across the child.
    // This optional projection cannot change the actual operation result.
    try{if((context.operation==ymcc::steamfailure::Operation::RebuildDocument||context.operation==ymcc::steamfailure::Operation::ReopenBigPicture)&&detail.contains("recoveryBudget")){
        const auto facts=normalizeRecoveryBudget(detail.at("recoveryBudget"));
        if(facts.is_object()&&facts.value("operation",std::string{})==ymcc::steamfailure::name(context.operation))row["recoveryBudget"]=facts;
    }}catch(...){}
    // Terminal no-effect receipts only; labels contain no account values or page data.
    try{if(context.phase==ymcc::steamfailure::Phase::Finished&&
        (context.operation==ymcc::steamfailure::Operation::RebuildDocument||context.operation==ymcc::steamfailure::Operation::ReopenBigPicture)&&
        detail.value("reason",Json{})=="decky-cleanup-account-unconfirmed"&&detail.value("ok",Json{})==false&&
        detail.value("mutated",Json{})==false&&detail.value("uncertain",Json(false))==false){
        const auto it=detail.find("sessionCheck");
        if(it!=detail.end()&&it->is_string())for(const auto* label:{"parent-account-source","shared-process-receipt","shared-process-delegation",
            "child-mode-read","child-account-mismatch","child-account-proof"})if(*it==label){row["cleanupAccountStage"]=label;break;}
        // The parent can return this proof before dispatch. It is not a claim
        // that the Runtime retained the mirror or actually scheduled a retry.
        if(it!=detail.end()&&(*it=="parent-account-source"||*it=="shared-process-receipt"||*it=="shared-process-delegation")&&
            detail.value("awaitCurrentIdentity",Json{})==true&&detail.value("cleanupNotStarted",Json{})==true)
            row["cleanupUnstartedIdentityWait"]=true;
    }}catch(...){}
    return row;
}

}
