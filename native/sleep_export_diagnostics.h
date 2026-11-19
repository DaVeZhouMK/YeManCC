#pragma once
// Export-only historical diagnostics. No Windows calls, timers or power actions.
#include "json.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ymcc { namespace sleepexport {
using Json=nlohmann::json;
inline constexpr std::uint32_t timeoutMs=10000;
inline constexpr std::uint32_t deadlineCooldownMs=60000;
inline constexpr std::uint32_t deadlineStreakWindowMs=300000;
inline constexpr unsigned maxDeadlineStreak=3;
inline constexpr std::uint32_t cooldownForStreak(unsigned streak) noexcept {
    return deadlineCooldownMs << ((std::max)(1u,(std::min)(streak,maxDeadlineStreak))-1);
}
// Historical DRIPS selection below is auxiliary only; System Power Report has no DRIPS or session-length gate here.
inline constexpr std::size_t maxReportBytes=8*1024*1024;
inline constexpr std::size_t maxFacts=256;
inline constexpr std::size_t maxFactBytes=128*1024;
struct QueryResult {
    bool ran=false;
    std::uint32_t exitCode=0,win32Error=0;
    std::string completion;
    std::uint64_t elapsedMs=0;
    std::string xml,readError;
};
// The original runCapture returned buffer, not a full child/pipe or codepage claim.
struct QueryFailureOutputBytes {
    static constexpr std::size_t capacity=2048;
    bool observed=false;
    std::uint64_t bufferBytes=0;
    std::size_t retainedBytes=0;
    unsigned char bytes[capacity]{};
    void observe(const std::string& value) noexcept {
        bufferBytes=value.size();retainedBytes=(std::min)(value.size(),capacity);
        for(std::size_t i=0;i<retainedBytes;++i)bytes[i]=static_cast<unsigned char>(value[i]);
        observed=true;
    }
};
inline Json queryFailureOutputBytesEvidence(const QueryFailureOutputBytes& value) noexcept {
    try {
        std::string hex;
        if(value.observed) {
            static constexpr char digits[]="0123456789abcdef";
            hex.reserve(value.retainedBytes*2);
            for(std::size_t i=0;i<value.retainedBytes;++i) {
                const auto byte=value.bytes[i];hex.push_back(digits[byte>>4]);hex.push_back(digits[byte&15]);
            }
        }
        Json result=Json::object();
        result["schema"]=1;result["encoding"]="hex-lowercase";
        result["bufferObserved"]=value.observed;
        result["bufferBytes"]=value.observed?Json(value.bufferBytes):Json(nullptr);
        result["retainedBytes"]=value.observed?Json(value.retainedBytes):Json(nullptr);
        result["maxRetainedBytes"]=QueryFailureOutputBytes::capacity;
        result["truncated"]=value.observed?Json(value.bufferBytes>QueryFailureOutputBytes::capacity):Json(nullptr);
        result["data"]=value.observed?Json(hex):Json(nullptr);
        result["codePageKnown"]=false;
        result["scope"]="current-failed-query-returned-buffer-prefix;not-complete-child-output";
        return result;
    } catch(...) { return nullptr; }
}
// Only the original post-capture finalization site, not all early setup exits.
struct CaptureFinalization {
    bool entered=false,jobTerminationAccepted=false;
    std::uint32_t jobTerminationError=0,rootWaitResult=0,rootWaitError=0;
    std::uint64_t jobTerminationElapsedMs=0,rootWaitElapsedMs=0;
};
struct CaptureLoopTiming {
    bool observed=false;
    std::uint64_t setupThroughResumeElapsedMs=0,loopElapsedMs=0;
    struct Pipe {
        bool emptyPeekObserved=false,nonemptyPeekObserved=false;
        bool emptyReadObserved=false,bytesReadObserved=false;
        std::optional<std::uint32_t> firstPeekError,firstReadError;
        bool observed() const noexcept {return emptyPeekObserved||nonemptyPeekObserved||firstPeekError.has_value();}
    } pipe;
    struct DeadlineJobAccounting {
        bool observed=false,available=false;
        std::uint32_t win32Error=0,totalProcesses=0,activeProcesses=0;
        std::uint64_t elapsedMs=0;
        std::int64_t userTime100ns=0,kernelTime100ns=0;
    } deadlineJob;
    struct DeadlineRootAccounting {
        bool observed=false,available=false;
        std::uint32_t win32Error=0;
        std::uint64_t elapsedMs=0,userTime100ns=0,kernelTime100ns=0;
    } deadlineRoot;
};
inline Json captureFinalizationEvidence(const CaptureFinalization& value) noexcept {
    try {
        Json result=Json::object();result["schema"]=1;
        result["scope"]="original-post-capture-job-termination-and-root-wait;not-all-resources-or-subtree-retirement-proof";
        result["entered"]=value.entered;result["rootWaitAllowanceMs"]=1000;
        result["jobTerminationAccepted"]=value.entered?Json(value.jobTerminationAccepted):Json(nullptr);
        result["jobTerminationError"]=value.entered?Json(value.jobTerminationError):Json(nullptr);
        result["rootWaitResult"]=value.entered?Json(value.rootWaitResult):Json(nullptr);
        result["rootWaitError"]=value.entered?Json(value.rootWaitError):Json(nullptr);
        result["jobTerminationElapsedMs"]=value.entered?Json(value.jobTerminationElapsedMs):Json(nullptr);
        result["rootWaitElapsedMs"]=value.entered?Json(value.rootWaitElapsedMs):Json(nullptr);
        return result;
    }catch(...){return nullptr;}
}
// Only a completed diagnostic deadline can suppress an immediate duplicate.
// This stores no successful report and grants no power or recovery decision.
struct DeadlineCooldown {
    bool known=false;
    std::uint64_t generation=0,completedTick=0;
    unsigned streak=0;
    std::string exportId;
    QueryResult observation;
    bool active(std::uint64_t currentGeneration,std::uint64_t now) const noexcept {
        return known&&generation==currentGeneration&&now>=completedTick&&
            now-completedTick<cooldownForStreak(streak);
    }
    void observe(std::uint64_t observedGeneration,std::uint64_t now,
                 const QueryResult& result,const std::string& operation) {
        const auto next=known && generation==observedGeneration && now>=completedTick &&
            now-completedTick<=deadlineStreakWindowMs ? (std::min)(streak+1,maxDeadlineStreak) : 1;
        known=false;streak=0;
        if(!result.ran||result.exitCode!=1460||result.win32Error!=0||
                result.completion!="deadline"||!result.readError.empty()||!result.xml.empty())return;
        observation=result;exportId=operation;generation=observedGeneration;completedTick=now;streak=next;known=true;
    }
    Json retryPolicy() const {
        return {{"schema",1},{"deadlineStreak",streak},{"cooldownMs",cooldownForStreak(streak)},
            {"streakWindowMs",deadlineStreakWindowMs},{"powerGeneration",std::to_string(generation)},
            {"completedTickMs",std::to_string(completedTick)}};
    }
};
inline bool unsignedNumber(const Json& value,std::uint64_t& out) {
    if(value.is_number_unsigned())out=value.get<std::uint64_t>();
    else if(value.is_number_integer()&&value.get<std::int64_t>()>=0)out=static_cast<std::uint64_t>(value.get<std::int64_t>());
    else return false;
    return out!=(std::numeric_limits<std::uint64_t>::max)();
}
inline Json anomalyFromLine(std::string_view line) {
    if(line.empty()||line.size()>maxFactBytes)return nullptr;
    auto fact=Json::parse(line.begin(),line.end(),nullptr,false);
    if(!fact.is_object()||!fact.contains("event")||fact["event"]!="s0-low-power-residency"||
        !fact.contains("details")||!fact["details"].is_object())return nullptr;
    const auto& d=fact["details"];
    if(!d.contains("source")||d["source"]!="Microsoft-Windows-Kernel-Power/507"||
        !d.contains("timing")||d["timing"]!="after-standby-exit"||
        !d.contains("assessment")||d["assessment"]!="no-software-drips-recorded"||
        !d.contains("values")||!d["values"].is_object())return nullptr;
    if(d.contains("invalidFields")&&(!d["invalidFields"].is_array()||!d["invalidFields"].empty()))return nullptr;
    if(d.contains("automaticRepairAuthorized")&&d["automaticRepairAuthorized"]!=false)return nullptr;
    const auto& v=d["values"];
    if(!v.contains("SleepEntered")||v["SleepEntered"]!=true)return nullptr;
    std::uint64_t duration=0,sw=0,hw=0,transitions=0;
    const auto number=[&](const char* key,std::uint64_t& out){return v.contains(key)&&unsignedNumber(v[key],out);};
    // Optional hardware/transition counters stay absent. Present values
    // must be valid zero; this diagnostic is selected by proven software zero.
    if(!number("DurationInUs",duration)||duration<120000000||
        !number("DripsResidencyInUs",sw)||sw!=0||
        (v.contains("HwDripsResidencyInUs")&&(!number("HwDripsResidencyInUs",hw)||hw!=0))||
        (v.contains("DripsTransitions")&&(!number("DripsTransitions",transitions)||transitions!=0)))return nullptr;
    for(const char* key:{"ActiveResidencyInUs","SleepDurationInUs","FirstDripsEntryInUs",
        "SleepSwDripsResidencyInUs","SleepHwDripsResidencyInUs"}) {
        if(!v.contains(key))continue;
        std::uint64_t value=0;
        if(!unsignedNumber(v[key],value)||value>duration||
            ((std::string(key)=="FirstDripsEntryInUs"||std::string(key)=="SleepSwDripsResidencyInUs"||
              std::string(key)=="SleepHwDripsResidencyInUs")&&value!=0))return nullptr;
    }
    return fact; // Preserve the original evidence and writer identity.
}
inline Json selectAnomaly(const std::vector<std::string>& facts) {
    const auto begin=facts.size()>maxFacts?facts.size()-maxFacts:0;
    for(auto i=facts.size();i>begin;--i) {
        auto selected=anomalyFromLine(facts[i-1]);
        if(!selected.is_null())return selected;
    }
    return nullptr;
}
inline Json selectAnomalyFromFileTail(std::string_view bytes,bool skipFirstLine) {
    if(skipFirstLine) {
        const auto first=bytes.find('\n');
        if(first==std::string_view::npos)return nullptr;
        bytes.remove_prefix(first+1);
    }
    if(bytes.empty())return nullptr;
    // A single final LF terminates a record; it does not add an empty record.
    auto end=bytes.size()-(bytes.back()=='\n'?1u:0u);
    std::size_t retained=0;
    while(retained<maxFacts) {
        const auto delimiter=end?bytes.rfind('\n',end-1):std::string_view::npos;
        const auto start=delimiter==std::string_view::npos?0:delimiter+1;
        if(end-start<=maxFactBytes) {
            ++retained;
            auto selected=anomalyFromLine(bytes.substr(start,end-start));
            if(!selected.is_null())return selected;
        }
        if(delimiter==std::string_view::npos)break;
        end=delimiter;
    }
    return nullptr;
}
// Encoding and prefix check only. Does not establish XML/schema validity or
// prove that this report contains, explains or repairs the selected session.
inline bool xmlPrefix(const std::string& xml) {
    std::size_t pos=0,width=1;bool little=true;
    const auto byte=[&](std::size_t at){return static_cast<unsigned char>(xml[at]);};
    if(xml.size()>=3&&byte(0)==0xef&&byte(1)==0xbb&&byte(2)==0xbf)pos=3;
    else if(xml.size()>=2&&((byte(0)==0xff&&byte(1)==0xfe)||(byte(0)==0xfe&&byte(1)==0xff))) {
        little=byte(0)==0xff;pos=2;width=2;
        if((xml.size()-pos)%2!=0)return false;
    }
    const auto unit=[&](std::size_t at)->unsigned {
        if(at>=xml.size()||xml.size()-at<width)return 0;
        if(width==1)return byte(at);
        return little?static_cast<unsigned>(byte(at))|(static_cast<unsigned>(byte(at+1))<<8):
            (static_cast<unsigned>(byte(at))<<8)|static_cast<unsigned>(byte(at+1));
    };
    const auto space=[](unsigned c){return c==' '||c=='\t'||c=='\r'||c=='\n';};
    while(pos<xml.size()&&space(unit(pos)))pos+=width;
    if(unit(pos)!='<')return false;
    pos+=width;const auto next=unit(pos);
    if((next>='A'&&next<='Z')||(next>='a'&&next<='z')||next=='_'||next==':')return true;
    if(next!='?')return false;
    pos+=width;
    for(const char c:std::string("xml")){if(unit(pos)!=static_cast<unsigned>(c))return false;pos+=width;}
    return space(unit(pos))||unit(pos)=='?';
}
template<class SelectAnomaly>
inline Json collectWithAnomaly(SelectAnomaly select,const std::function<bool()>& current,
                    const std::function<QueryResult(std::uint32_t)>& query,
                    const DeadlineCooldown* recentDeadline=nullptr,std::uint64_t generation=0,std::uint64_t now=0) {
    Json receipt={{"schema","ymcc.sleep-export-diagnostics.v1"},{"diagnostic","WindowsSystemPowerReport"},{"collectionTrigger","explicit-log-export;DRIPS-independent"},
        {"dripsRole","auxiliary-only"},{"targetPowerState","UNKNOWN"},
        {"status","UNKNOWN"},{"reportAvailable",false},{"automaticRepairAuthorized",false},
        {"detailLogsRequired",false},{"mandatoryEvidence",true},{"diagnosticOnly",true},
        {"powerActionAuthorized",false},{"sleepSuccessWitness",false},{"selectedSessionInReportVerified",false},
        {"queryAttempts",0},{"timeoutMs",timeoutMs},{"maxReportBytes",maxReportBytes},
        {"selectedEvidence",nullptr},{"selectedEvidenceIdentity",nullptr}};
    try {
        auto selected=select();
        // Optional auxiliary accounting never decides whether Windows diagnostics are collected.
        // Empty, absent, positive or invalid DRIPS facts still use the same explicit export query.
        receipt["sessionCoverage"]="unverified; report availability is not proof of target inclusion or blocker attribution";
        Json identity=Json::object();
        if(selected.is_object())for(const char* key:{"time","timestampUtc","timestampLocal","utcEpochMs","writerInstanceId","writerPid",
            "writerProcessCreation100ns","writerSequence","writerPowerGeneration"})if(selected.contains(key))identity[key]=selected[key];
        if(selected.is_object()&&selected["details"].contains("fileTime"))identity["originalEventFileTime"]=selected["details"]["fileTime"];
        if(!identity.empty())receipt["selectedEvidenceIdentity"]=std::move(identity);
        receipt["selectedEvidence"]=std::move(selected);
        if(!current||!current()){receipt["reason"]="export-context-not-current";return receipt;}
        if(recentDeadline&&recentDeadline->active(generation,now)) {
            if(!current()){receipt["reason"]="export-context-changed";return receipt;}
            const auto& previous=recentDeadline->observation;
            const auto age=now-recentDeadline->completedTick;
            const auto cooldown=cooldownForStreak(recentDeadline->streak);
            receipt["reason"]="recent-query-deadline";
            receipt["querySuppressed"]=true;
            receipt["recentDeadline"]={{"scope","previous-query-only;not-a-current-query-or-report"},
                {"originalExportId",recentDeadline->exportId},{"powerGeneration",std::to_string(recentDeadline->generation)},
                {"completedTickMs",std::to_string(recentDeadline->completedTick)},
                {"ageMs",age},{"retryAfterMs",cooldown-age},{"cooldownMs",cooldown},
                {"deadlineStreak",recentDeadline->streak},{"streakWindowMs",deadlineStreakWindowMs},
                {"query",{{"ran",previous.ran},{"exitCode",previous.exitCode},{"win32Error",previous.win32Error},
                    {"completion",previous.completion},{"elapsedMs",previous.elapsedMs},{"readError",previous.readError}}}};
            return receipt;
        }
        receipt["queryAttempts"]=1;
        QueryResult result;bool threw=false;
        try {result=query(timeoutMs);}catch(...){threw=true;}
        // Even an unsuccessful/throwing query must recheck the export generation.
        if(!current()){receipt["reason"]="export-context-changed";return receipt;}
        receipt["query"]={{"ran",result.ran},{"exitCode",result.exitCode},{"win32Error",result.win32Error},
            {"completion",result.completion},{"elapsedMs",result.elapsedMs},{"readError",result.readError}};
        if(threw){receipt["reason"]="query-exception";return receipt;}
        if(!result.ran||result.exitCode!=0||result.win32Error!=0||result.completion!="child-exit") {
            receipt["reason"]="query-not-completed-successfully";return receipt;
        }
        if(!result.readError.empty()){receipt["reason"]="report-read-failed";return receipt;}
        receipt["reportBytes"]=result.xml.size();
        if(result.xml.empty()||result.xml.size()>maxReportBytes||!xmlPrefix(result.xml)) {
            receipt["reason"]="report-empty-oversize-or-invalid-prefix";return receipt;
        }
        receipt["status"]="AVAILABLE";receipt["reportAvailable"]=true;
        receipt["reportValidation"]="encoding-and-xml-prefix-only";receipt["reason"]="diagnostic-report-available";
    }catch(...){receipt["status"]="UNKNOWN";receipt["reportAvailable"]=false;receipt["reason"]="diagnostic-exception";}
    return receipt;
}
inline Json collect(const std::vector<std::string>& facts,const std::function<bool()>& current,
                    const std::function<QueryResult(std::uint32_t)>& query,
                    const DeadlineCooldown* recentDeadline=nullptr,std::uint64_t generation=0,std::uint64_t now=0) {
    return collectWithAnomaly([&]{return selectAnomaly(facts);},current,query,recentDeadline,generation,now);
}
}} // namespace ymcc::sleepexport
