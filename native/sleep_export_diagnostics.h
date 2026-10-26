#pragma once
// Export-only historical diagnostics. No Windows calls, timers or power actions.
#include "json.hpp"
#include <algorithm>
#include <cstdint>
#include <functional>
#include <limits>
#include <string>
#include <vector>

namespace ymcc { namespace sleepexport {
using Json=nlohmann::json;
inline constexpr std::uint32_t timeoutMs=8000;
// Microsoft documents that SleepStudy omits sessions shorter than ten minutes.
// Preserve shorter anomalies as UNKNOWN without spending a report query.
inline constexpr std::uint64_t sleepStudyMinimumSessionUs=600000000;
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
inline bool unsignedNumber(const Json& value,std::uint64_t& out) {
    if(value.is_number_unsigned())out=value.get<std::uint64_t>();
    else if(value.is_number_integer()&&value.get<std::int64_t>()>=0)out=static_cast<std::uint64_t>(value.get<std::int64_t>());
    else return false;
    return out!=(std::numeric_limits<std::uint64_t>::max)();
}
inline Json selectAnomaly(const std::vector<std::string>& facts) {
    const auto begin=facts.size()>maxFacts?facts.size()-maxFacts:0;
    for(auto i=facts.size();i>begin;--i) {
        const auto& line=facts[i-1];
        if(line.empty()||line.size()>maxFactBytes)continue;
        auto fact=Json::parse(line,nullptr,false);
        if(!fact.is_object()||!fact.contains("event")||fact["event"]!="s0-low-power-residency"||
            !fact.contains("details")||!fact["details"].is_object())continue;
        const auto& d=fact["details"];
        if(!d.contains("source")||d["source"]!="Microsoft-Windows-Kernel-Power/507"||
            !d.contains("timing")||d["timing"]!="after-standby-exit"||
            !d.contains("assessment")||d["assessment"]!="no-software-drips-recorded"||
            !d.contains("values")||!d["values"].is_object())continue;
        if(d.contains("invalidFields")&&(!d["invalidFields"].is_array()||!d["invalidFields"].empty()))continue;
        if(d.contains("automaticRepairAuthorized")&&d["automaticRepairAuthorized"]!=false)continue;
        const auto& v=d["values"];
        if(!v.contains("SleepEntered")||v["SleepEntered"]!=true)continue;
        std::uint64_t duration=0,sw=0,hw=0,transitions=0;
        const auto number=[&](const char* key,std::uint64_t& out){return v.contains(key)&&unsignedNumber(v[key],out);};
        // Optional hardware/transition counters stay absent. Present values
        // must be valid zero; this diagnostic is selected by proven software zero.
        if(!number("DurationInUs",duration)||duration<120000000||
            !number("DripsResidencyInUs",sw)||sw!=0||
            (v.contains("HwDripsResidencyInUs")&&(!number("HwDripsResidencyInUs",hw)||hw!=0))||
            (v.contains("DripsTransitions")&&(!number("DripsTransitions",transitions)||transitions!=0)))continue;
        bool consistent=true;
        for(const char* key:{"ActiveResidencyInUs","SleepDurationInUs","FirstDripsEntryInUs",
            "SleepSwDripsResidencyInUs","SleepHwDripsResidencyInUs"}) {
            if(!v.contains(key))continue;
            std::uint64_t value=0;
            if(!unsignedNumber(v[key],value)||value>duration||
                ((std::string(key)=="FirstDripsEntryInUs"||std::string(key)=="SleepSwDripsResidencyInUs"||
                  std::string(key)=="SleepHwDripsResidencyInUs")&&value!=0)){consistent=false;break;}
        }
        if(consistent)return fact; // Preserve the original evidence and writer identity.
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
inline Json collect(const std::vector<std::string>& facts,const std::function<bool()>& current,
                    const std::function<QueryResult(std::uint32_t)>& query) {
    Json receipt={{"schema","ymcc.sleep-export-diagnostics.v1"},{"diagnostic","WindowsSleepStudy"},
        {"status","UNKNOWN"},{"reportAvailable",false},{"automaticRepairAuthorized",false},
        {"detailLogsRequired",false},{"mandatoryEvidence",true},{"diagnosticOnly",true},
        {"powerActionAuthorized",false},{"sleepSuccessWitness",false},{"selectedSessionInReportVerified",false},
        {"queryAttempts",0},{"timeoutMs",timeoutMs},{"maxReportBytes",maxReportBytes},
        {"selectedEvidence",nullptr},{"selectedEvidenceIdentity",nullptr}};
    try {
        const auto selected=selectAnomaly(facts);
        if(selected.is_null()){receipt["status"]="NOT_REQUIRED";receipt["reason"]="no-qualifying-historical-anomaly";return receipt;}
        receipt["selectedEvidence"]=selected;
        receipt["sleepStudyDocumentedMinimumSessionUs"]=sleepStudyMinimumSessionUs;
        receipt["sessionCoverage"]="unverified; report availability is not proof of target inclusion or blocker attribution";
        Json identity=Json::object();
        for(const char* key:{"time","timestampUtc","timestampLocal","utcEpochMs","writerInstanceId","writerPid",
            "writerProcessCreation100ns","writerSequence","writerPowerGeneration"})if(selected.contains(key))identity[key]=selected[key];
        if(selected["details"].contains("fileTime"))identity["originalEventFileTime"]=selected["details"]["fileTime"];
        receipt["selectedEvidenceIdentity"]=std::move(identity);
        if(selected["details"]["values"]["DurationInUs"].get<std::uint64_t>()<sleepStudyMinimumSessionUs) {
            receipt["reason"]="session-below-documented-sleepstudy-minimum";return receipt;
        }
        if(!current||!current()){receipt["reason"]="export-context-not-current";return receipt;}
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
}} // namespace ymcc::sleepexport
