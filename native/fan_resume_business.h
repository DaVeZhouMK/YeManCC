#pragma once
#include <cstdint>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
namespace ymcc::fan_session {
struct ResumeBusinessReply {
    bool known=false,accepted=false,grantPending=false;
    std::uint64_t low=0,high=0,fanGeneration=0,nativeGeneration=0,attempt=0;
    unsigned hostGrantsObserved=0;
    std::string phase,reason,attemptStatus,attemptOutcome;
    bool matches(std::uint64_t requestLow,std::uint64_t requestHigh,
                 std::uint64_t fan,std::uint64_t native) const noexcept {
        return known&&accepted&&low==requestLow&&high==requestHigh&&
            fanGeneration==fan&&nativeGeneration==native;
    }
};
inline bool businessDecimal(const nlohmann::json& value,std::uint64_t& output) {
    output=0;if(!value.is_string())return false;const auto& text=value.get_ref<const std::string&>();
    if(text.empty()||text.size()>20||(text.size()>1&&text[0]=='0'))return false;
    std::uint64_t parsed=0;
    for(const char c:text){if(c<'0'||c>'9')return false;const auto digit=static_cast<unsigned>(c-'0');
        if(parsed>((std::numeric_limits<std::uint64_t>::max)()-digit)/10)return false;parsed=parsed*10+digit;}
    output=parsed;return true;
}
inline bool businessHex(const nlohmann::json& value,std::uint64_t& output) {
    output=0;if(!value.is_string())return false;const auto& text=value.get_ref<const std::string&>();
    if(text.size()!=16)return false;std::uint64_t parsed=0;
    for(const char c:text){unsigned digit;if(c>='0'&&c<='9')digit=static_cast<unsigned>(c-'0');
        else if(c>='a'&&c<='f')digit=static_cast<unsigned>(c-'a')+10u;else return false;parsed=(parsed<<4)|digit;}
    output=parsed;return true;
}
inline std::string businessHex(std::uint64_t value) {
    std::string output(16,'0');static constexpr char hex[]="0123456789abcdef";
    for(unsigned i=0;i<16;++i){output[15-i]=hex[value&15u];value>>=4;}return output;
}
inline std::string businessStatePath(std::uint64_t low,std::uint64_t high) {
    return "/api/state?resumeRequestLow="+businessHex(low)+"&resumeRequestHigh="+businessHex(high);
}
inline bool parseBusinessBody(const std::string& body,nlohmann::json& root,ResumeBusinessReply& output) noexcept {
    output={};root=nullptr;
    try {
        using J=nlohmann::json;std::vector<std::set<std::string>> keys;
        root=J::parse(body,[&](int depth,J::parse_event_t event,J& value){
            if(depth>32)throw std::invalid_argument("resume-business-depth");
            if(event==J::parse_event_t::object_start)keys.emplace_back();
            else if(event==J::parse_event_t::object_end){if(keys.empty())throw std::invalid_argument("resume-business-object");keys.pop_back();}
            else if(event==J::parse_event_t::key&&
                (keys.empty()||!keys.back().insert(value.get<std::string>()).second))throw std::invalid_argument("resume-business-duplicate");
            return true;
        });
        if(!root.is_object()||!keys.empty())return false;
        const auto field=root.find("resumeBusiness");if(field==root.end()||field->is_null())return true;
        const auto& row=*field;ResumeBusinessReply parsed;
        if(!row.is_object()||row.at("schemaVersion")!=1||!row.at("schemaVersion").is_number_integer()||
            row.at("encoding")!="native-f5-deadline-v1"||!row.at("accepted").is_boolean()||!row.at("accepted").get<bool>()||
            !businessHex(row.at("requestLow"),parsed.low)||!businessHex(row.at("requestHigh"),parsed.high)||
            !businessDecimal(row.at("fanGeneration"),parsed.fanGeneration)||!parsed.fanGeneration||
            !businessDecimal(row.at("nativeGeneration"),parsed.nativeGeneration)||!parsed.nativeGeneration||
            !row.at("phase").is_string()||!row.at("reason").is_string()||!row.at("grantPending").is_boolean()||
            !row.at("grantBudgetUsed").is_null()||!row.at("hostGrantsObserved").is_number_unsigned())return false;
        parsed.phase=row.at("phase").get<std::string>();parsed.reason=row.at("reason").get<std::string>();
        if(parsed.phase!="Running"&&parsed.phase!="AwaitingGrant"&&parsed.phase!="PendingHandoff"&&parsed.phase!="Terminal")return false;
        parsed.grantPending=row.at("grantPending").get<bool>();if(parsed.grantPending!=(parsed.phase=="PendingHandoff"))return false;
        const auto grants=row.at("hostGrantsObserved").get<std::uint64_t>();if(grants<1||grants>3||parsed.reason.size()>192)return false;
        parsed.hostGrantsObserved=static_cast<unsigned>(grants);const auto& attempt=row.at("currentAttempt");
        if(!attempt.is_object()||!businessDecimal(attempt.at("id"),parsed.attempt)||!parsed.attempt||
            !attempt.at("status").is_string()||(!attempt.at("outcome").is_null()&&!attempt.at("outcome").is_string()))return false;
        parsed.attemptStatus=attempt.at("status").get<std::string>();
        if(parsed.attemptStatus!="pending"&&parsed.attemptStatus!="executing"&&parsed.attemptStatus!="completed"&&parsed.attemptStatus!="failed"&&parsed.attemptStatus!="superseded")return false;
        if(attempt.at("outcome").is_string())parsed.attemptOutcome=attempt.at("outcome").get<std::string>();
        parsed.known=parsed.accepted=true;output=std::move(parsed);return true;
    }catch(...){root=nullptr;output={};return false;}
}
}
