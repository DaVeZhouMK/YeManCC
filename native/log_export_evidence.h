#pragma once
// Export-time file/staging evidence only; no hardware, controllers or polling.
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <algorithm>
#include <system_error>
#include "json.hpp"
namespace ymcc::logexport {
using EvidenceJson=nlohmann::json;
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
    for(const auto* exact:{"input-capture.jsonl","native-lifecycle.log","native-detail.log","monitor-detail.log","tdp.log","tdp-detail.log",
        "frontend-errors.log","frontend-trace.log","frontend-trace-detail.log","webview-failures.log","fan-api.log",
        "fan-lifecycle.log","gyro-motion.log","virtual-gamepad.log","focus-trace.log","hardware-coordinator.log",
        "input-host-fatal.log","input-host-rejects.log","input-host-cost.log","input-host-signal.log","input-host-report-chain.log","input-host-xbox-sdk.log",
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
inline EvidenceJson stagedFileEvidence(const std::filesystem::path& staged,const std::string& entry) noexcept {
    try {
        EvidenceJson row={{"entry",entry},{"evidenceScope","actual-staged-bytes-not-live-source"},{"originalBytesRetained",true}};
        std::ifstream in(staged,std::ios::binary);if(!in){row["status"]="staged-read-error";return row;}
        in.seekg(0,std::ios::end);const auto end=in.tellg();if(end<0){row["status"]="staged-size-error";return row;}
        const auto size=static_cast<unsigned long long>(end);row["bytes"]=size;
        constexpr unsigned long long limit=8ULL*1024*1024;
        const auto offset=size>limit?size-limit:0ULL;
        std::string text(static_cast<size_t>(size-offset),'\0');in.seekg(static_cast<std::streamoff>(offset));
        if(!text.empty())in.read(text.data(),static_cast<std::streamsize>(text.size()));
        if(!in){row["status"]="staged-read-error";return row;}
        row["earlierBytesNotInspected"]=offset;row["inspectionScope"]=offset?"retained-tail-only":"retained-file-only";
        const bool jsonl=jsonLinesEvidenceName(entry);row["format"]=jsonl?"json-lines":"opaque-file";
        row["endsWithNewline"]=size==0 || (!text.empty()&&text.back()=='\n');row["status"]="inspected";
        if(!jsonl)return row;
        size_t cursor=0;if(offset){const auto first=text.find('\n');cursor=first==std::string::npos?text.size():first+1;row["leadingBoundaryLineNotInspected"]=true;}
        unsigned long long valid=0,invalid=0,oversize=0;std::string firstTime,lastTime,firstTimeField,lastTimeField;
        bool partialTail=false,oversizeTail=false;
        while(cursor<text.size()){
            const auto newline=text.find('\n',cursor);const bool terminal=newline==std::string::npos;
            const size_t count=terminal?text.size()-cursor:newline-cursor;
            if(count>65536){++oversize;if(terminal)oversizeTail=true;}
            else if(count){
                try {auto value=EvidenceJson::parse(text.substr(cursor,count));++valid;
                    std::string time,timeField;for(const auto* key:{"timestampUtc","timeWithOffset","timestampLocal","atUtc","time"})
                        if(value.is_object()&&value.contains(key)&&value[key].is_string()){time=value[key].get<std::string>();timeField=key;break;}
                    if(!time.empty()){if(firstTime.empty()){firstTime=time;firstTimeField=timeField;}lastTime=time;lastTimeField=timeField;}
                }catch(...){++invalid;if(terminal)partialTail=true;}
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
    }catch(...){return {{"entry",entry},{"status","staged-inspection-error"},{"originalBytesRetained",true}};}
}
} // namespace ymcc::logexport
