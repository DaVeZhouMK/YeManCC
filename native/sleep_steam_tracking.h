#pragma once
// Pure Steam evidence parser. Logs are observations, not commands. No process,
// registry, Steam UI, controller or fan mutations exist in this header.
#include <string>
#include <vector>
#include <map>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <sstream>
#include "json.hpp"
namespace ymcc::sleepsteam {
using Json=nlohmann::json;
inline bool unsignedDecimal(const std::string& s,std::uint64_t& value) {
    if(s.empty())return false;value=0;
    for(const auto c:s){if(c<'0'||c>'9')return false;const auto digit=static_cast<unsigned>(c-'0');
        if(value>(std::numeric_limits<std::uint64_t>::max()-digit)/10)return false;value=value*10+digit;}
    return true;
}
inline bool appId(const std::string& raw,std::uint32_t& id) {
    if(raw.empty())return false;std::uint64_t n=0;
    if(raw[0]=='-') {
        if(!unsignedDecimal(raw.substr(1),n)||!n||n>0x80000000ULL)return false;
        id=static_cast<std::uint32_t>(0x100000000ULL-n);return true;
    }
    if(!unsignedDecimal(raw,n)||!n)return false;
    if(n<=0xffffffffULL){id=static_cast<std::uint32_t>(n);return true;}
    // Steam shortcut GameID encodes the AppID in the upper32, type=2 in lower32.
    // Never round this 64-bit decimal through JS Number or truncate an arbitrary ID.
    if((n&0xffffffffULL)!=0x02000000ULL||!(n>>32))return false;
    id=static_cast<std::uint32_t>(n>>32);return true;
}
inline std::string pathKey(std::string s) {
    std::replace(s.begin(),s.end(),'/','\\');
    for(auto& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');
    if(s.rfind("\\\\?\\unc\\",0)==0)s="\\\\"+s.substr(8);
    else if(s.rfind("\\\\?\\",0)==0)s=s.substr(4);
    return s;
}
inline std::string commandExe(const std::string& raw) {
    size_t start=raw.find_first_not_of(" \t");if(start==std::string::npos)return {};
    if(raw[start]=='"') {
        while(start<raw.size()&&raw[start]=='"')++start;
        const auto end=raw.find('"',start);return end==std::string::npos?std::string():raw.substr(start,end-start);
    }
    const auto end=raw.find_first_of(" \t",start);return raw.substr(start,end==std::string::npos?raw.size()-start:end-start);
}
inline bool timestamp(const std::string& line,std::string& at) {
    if(line.size()<22||line[0]!='['||line[20]!=']'||line[21]!=' ')return false;
    at=line.substr(1,19);
    for(size_t i=0;i<at.size();++i){const bool sep=i==4||i==7||i==10||i==13||i==16;
        const char expected=i==4||i==7?'-':i==10?' ':':';if(sep?at[i]!=expected:at[i]<'0'||at[i]>'9')return false;}
    return at.substr(5,2)>="01"&&at.substr(5,2)<="12"&&at.substr(8,2)>="01"&&at.substr(8,2)<="31"&&
        at.substr(11,2)<="23"&&at.substr(14,2)<="59"&&at.substr(17,2)<="59";
}
// Match readTail's existing 2 MiB complete-snapshot budget. A row-count cap
// can reject dense but complete logs before their final drop/client-reset row.
inline constexpr std::size_t trackerMaximumBytes=2*1024*1024;
inline bool trackerLinesWithinBudget(const std::vector<std::string>& lines) {
    std::size_t bytes=0;
    for(std::size_t i=0;i<lines.size();++i){
        const std::size_t separator=i?1:0;
        if(separator>trackerMaximumBytes-bytes||lines[i].size()>trackerMaximumBytes-bytes-separator)return false;
        bytes+=separator+lines[i].size();
    }
    return true;
}
// UI running-list changes and explicit process lifetime changes are different
// observations. Recovery still requires the native original record/primary;
// its log join keeps an exact live-PID launch row across a UI list removal.
enum class TrackingScope { RunningListAndProcess, ProcessLifetime };
struct Binding {
    std::string status="unknown",at;
    std::uint32_t appid=0;
    unsigned line=0;
    bool exactPath=false;
};
inline Binding track(const std::vector<std::string>& lines,std::uint32_t pid,const std::string& exe,
    const std::string& earliest,const std::string& latest,
    TrackingScope scope=TrackingScope::RunningListAndProcess) {
    Binding result;if(!pid||exe.empty()||earliest.empty()||latest.empty()||earliest>latest)return result;
    if(!trackerLinesWithinBudget(lines)){result.status="incomplete";return result;}
    std::map<std::uint32_t,Binding> live;bool removed=false;unsigned scanned=0;
    for(const auto& row:lines) {
        ++scanned;
        std::string at;if(!timestamp(row,at))continue;const auto text=row.substr(22);
        if(text.rfind("Client version:",0)==0){live.clear();removed=false;continue;}
        if(text.rfind("Remove ",0)==0) {
            const auto end=text.find(" from running list",7);std::uint32_t id=0;
            if(scope==TrackingScope::RunningListAndProcess&&end!=std::string::npos&&
                appId(text.substr(7,end-7),id)&&live.erase(id))removed=true;
            continue;
        }
        if(text.rfind("AppID ",0)!=0)continue;
        const auto sep=text.find(' ',6);if(sep==std::string::npos)continue;std::uint32_t id=0;
        if(!appId(text.substr(6,sep-6),id))continue;
        const auto action=text.substr(sep);const std::string adding=" adding PID ",dropping=" no longer tracking PID ";
        const bool add=action.rfind(adding,0)==0,drop=action.rfind(dropping,0)==0;if(!add&&!drop)continue;
        const auto begin=add?adding.size():dropping.size();const auto end=action.find_first_of(" ,",begin);std::uint64_t candidate=0;
        if(end==std::string::npos||!unsignedDecimal(action.substr(begin,end-begin),candidate)||candidate!=pid)continue;
        if(drop){if(live.erase(id))removed=true;continue;}
        const std::string marker=" as a tracked process ";if(action.compare(end,marker.size(),marker)!=0)continue;
        if(at<earliest||at>latest||pathKey(commandExe(action.substr(end+marker.size())))!=pathKey(exe))continue;
        Binding b;b.status="tracked";b.at=at;b.appid=id;b.line=scanned;b.exactPath=true;live[id]=b;
    }
    if(live.size()==1)return live.begin()->second;
    result.status=live.empty()?(removed?"removed":"unknown"):"ambiguous";return result;
}
// Native ProcID observations identify the local game-record primary, not every
// member of its tracked PID vector. Labels are metadata, never OS path authority.
struct NativePrimaryProcessRow {std::uint32_t appid=0,pid=0;std::string at,label;bool local=false,removed=false;unsigned line=0;};
struct NativePrimaryProcessInventory {bool complete=true;std::string reason;std::map<std::uint32_t,NativePrimaryProcessRow> rows;};
// Match the existing complete-file read budget. Console logs can exceed 20,000
// short unrelated lines within 2 MiB; inspect the whole bounded snapshot so a
// later primary update/removal cannot be missed or authorize an older record.
inline constexpr std::size_t nativePrimaryMaximumBytes=2U*1024U*1024U;
inline NativePrimaryProcessInventory nativePrimaryProcessInventoryText(const std::string& bytes,const std::string& earliest,const std::string& latest) {
    NativePrimaryProcessInventory result;unsigned scanned=0;
    if(earliest.empty()||latest.empty()||earliest>latest){result.complete=false;result.reason="steam-native-primary-boundary-invalid";return result;}
    if(bytes.size()>nativePrimaryMaximumBytes){result.complete=false;result.reason="steam-native-primary-byte-limit";return result;}
    std::istringstream input(bytes);std::string line;
    while(std::getline(input,line)){
        ++scanned;
        std::string at;if(!timestamp(line,at)){
            if(line.find("Game process added : AppID ")!=std::string::npos||line.find("Game process updated : AppID ")!=std::string::npos||line.find("Game process removed: AppID ")!=std::string::npos){result.complete=false;result.reason="steam-native-primary-diagnostic-malformed";return result;}
            continue;
        }
        if(at<earliest||at>latest)continue;
        auto text=line.substr(22);if(!text.empty()&&text.back()=='\r')text.pop_back();size_t begin=0;bool removed=false;
        const std::string added="Game process added : AppID ",updated="Game process updated : AppID ",drop="Game process removed: AppID ";
        if(text.rfind(added,0)==0)begin=added.size();else if(text.rfind(updated,0)==0)begin=updated.size();else if(text.rfind(drop,0)==0){begin=drop.size();removed=true;}else continue;
        const auto sep=text.find(' ',begin),proc=text.rfind(", ProcID ");std::uint32_t app=0;std::uint64_t pid=0;
        if(sep==std::string::npos||proc==std::string::npos||proc<=sep||!appId(text.substr(begin,sep-begin),app)){
            result.complete=false;result.reason="steam-native-primary-diagnostic-malformed";return result;}
        const auto pidBegin=proc+9,pidEnd=text.find_first_not_of("0123456789",pidBegin);
        if(pidBegin==pidEnd||!unsignedDecimal(text.substr(pidBegin,pidEnd==std::string::npos?std::string::npos:pidEnd-pidBegin),pid)||!pid||pid>0xffffffffULL){
            result.complete=false;result.reason="steam-native-primary-diagnostic-malformed";return result;}
        bool local=false;
        if(!removed){if(pidEnd==std::string::npos||text.compare(pidEnd,5,", IP ")!=0){result.complete=false;result.reason="steam-native-primary-diagnostic-malformed";return result;}local=text.substr(pidEnd+5)=="0.0.0.0:0";}
        else if(pidEnd!=std::string::npos&&text.find_first_not_of(" \t\r",pidEnd)!=std::string::npos){result.complete=false;result.reason="steam-native-primary-diagnostic-malformed";return result;}
        result.rows[app]={app,static_cast<std::uint32_t>(pid),at,text.substr(sep+1,proc-sep-1),local,removed,scanned};
        if(result.rows.size()>32){result.complete=false;result.reason="steam-native-primary-record-limit";return result;}
    }
    return result;
}

// Pure fixture adapter; production parses the bounded text directly and does
// not allocate a vector for every console line. Preserve true line numbers.
inline NativePrimaryProcessInventory nativePrimaryProcessInventory(const std::vector<std::string>& lines,const std::string& earliest,const std::string& latest) {
    std::size_t count=0;
    for(std::size_t i=0;i<lines.size();++i){
        const std::size_t separator=i?1:0;
        if(separator>nativePrimaryMaximumBytes-count||lines[i].size()>nativePrimaryMaximumBytes-count-separator){
            NativePrimaryProcessInventory result;result.complete=false;result.reason="steam-native-primary-byte-limit";return result;}
        count+=separator+lines[i].size();
    }
    std::string bytes;bytes.reserve(count);
    for(std::size_t i=0;i<lines.size();++i){if(i)bytes.push_back('\n');bytes+=lines[i];}
    return nativePrimaryProcessInventoryText(bytes,earliest,latest);
}

inline std::uint64_t number(const Json& j,const char* key) {
    const auto i=j.find(key);if(i==j.end())return 0;
    if(i->is_number_unsigned())return i->get<std::uint64_t>();
    if(i->is_number_integer()&&i->get<std::int64_t>()>=0)return static_cast<std::uint64_t>(i->get<std::int64_t>());return 0;
}
inline bool flag(const Json& j,const char* key) {const auto i=j.find(key);return i!=j.end()&&i->is_boolean()&&i->get<bool>();}
inline bool exactOverviewGameId(const Json& overview,std::uint32_t appid) {
    const auto i=overview.find("gameidString");if(i==overview.end()||!i->is_string())return false;
    std::uint32_t normalized=0;return appId(i->get<std::string>(),normalized)&&normalized==appid;
}
inline Json verdict(const Json& steam,const Binding& binding,bool sameProcess,bool contactProof=false) {
    Json v={{"scope","steam"},{"status","unknown"},{"reason","steam-target-not-bound"},{"repairable",false},
        {"contactConnected",false},{"contactRepairable",false},{"associationVerified",false},
        {"continueButtonVerified",false},{"healthVerified",false},{"gameInput","not-tested"}};
    if(!sameProcess){v["reason"]="original-game-identity-unavailable";return v;}
    if(binding.status!="tracked"){v["reason"]="steam-tracker-"+binding.status;return v;}
    v["appid"]=binding.appid;
    if(!flag(steam,"ok")){v["reason"]="steam-read-unavailable";return v;}
    const auto list=steam.find("overviews");if(list==steam.end()||!list->is_array())return v;
    for(const auto& overview:*list)if(number(overview,"appid")==binding.appid) {
        if(!flag(overview,"overviewAvailable")){v["reason"]="steam-target-overview-unavailable";return v;}
        const bool exactContact=contactProof&&exactOverviewGameId(overview,binding.appid);
        v["contactConnected"]=exactContact;
        if(!flag(overview,"localRunning")) {
            // Steam may retain the original non-Steam tracker while its local
            // UI consumer drops RunningApps. Only exact original-instance
            // contact proof may spend one Valve scope call.
            if(exactContact&&overview.contains("localDisplayStatus")&&overview["localDisplayStatus"].is_number_integer()&&overview["localDisplayStatus"].get<int>()==11) {
                v["status"]="contact-degraded";v["reason"]="steam-running-consumer-missing";v["contactRepairable"]=true;return v;
            }
            v["status"]="blocked";v["reason"]=overview.contains("localDisplayStatus")&&overview["localDisplayStatus"].is_number()?"steam-backend-not-running":"steam-local-state-unavailable";return v;
        }
        if(flag(overview,"inRunningList")){v["status"]="current";v["reason"]="steam-association-current";v["associationVerified"]=true;return v;}
        v["status"]="stale-ui";v["reason"]="steam-running-overview-missing-from-ui";v["repairable"]=true;return v;
    }
    v["reason"]="steam-target-overview-unavailable";return v;
}
}
