#pragma once
#include "monitor_hwinfo_config.h"
#include "../deps/json/json.hpp"
#include <set>
#include <filesystem>

namespace ymcc::hwinfo {
using SensorJson=nlohmann::json;
inline constexpr const char* verifiedSensorImage="e4ce0c85c4288fa7e8eb12ebeece99efff4d81e0b13de53f091daabc81a3c9d3";
struct SensorState {bool exists=false;DWORD value=0;bool operator==(const SensorState&v)const{return exists==v.exists&&(!exists||value==v.value);}};
inline bool sensorGroupAllowed(const std::wstring& group){if(group==L"F00F5000_0")return true;for(const auto* family:{L"F0000101_",L"F0000302_"}){const std::wstring prefix=family;if(group.compare(0,prefix.size(),prefix)!=0)continue;const auto suffix=group.substr(prefix.size());unsigned n=0;return number(suffix,n)&&n<=31&&suffix==std::to_wstring(n);}return false;}
inline std::vector<std::wstring> sensorGroups(){std::vector<std::wstring> groups;groups.reserve(65);for(const auto* family:{L"F0000101_",L"F0000302_"})for(unsigned i=0;i<32;++i)groups.push_back(std::wstring(family)+std::to_wstring(i));groups.push_back(L"F00F5000_0");return groups;}
inline std::string ascii(const std::wstring&s){std::string out;for(auto c:s){require(c>0&&c<128,"Non-ASCII fixed sensor identity");out+=static_cast<char>(c);}return out;}
inline std::wstring sensorWide(const std::string&s){if(s.empty())return {};const int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);require(n>0,"Sensor receipt text is not UTF8");std::wstring w(n,L'\0');require(MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),w.data(),n)==n,"Sensor receipt text is not UTF8");return w;}
inline std::string sensorUtf8(const std::wstring&w){if(w.empty())return {};const int n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),nullptr,0,nullptr,nullptr);require(n>0,"Sensor INI identity invalid");std::string s(n,'\0');require(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,w.data(),static_cast<int>(w.size()),s.data(),n,nullptr,nullptr)==n,"Sensor INI identity invalid");return s;}
inline std::wstring sensorPath(const std::wstring&path){require(path.find(L'\0')==std::wstring::npos,"Invalid sensor receipt path");const DWORD n=GetFullPathNameW(path.c_str(),0,nullptr,nullptr);require(n>0&&n<32768,"Sensor receipt path unavailable");std::wstring full(n,L'\0');const auto got=GetFullPathNameW(path.c_str(),n,full.data(),nullptr);require(got>0&&got<n,"Sensor receipt path unavailable");full.resize(got);return lower(full);}
inline SensorState sensorState(const SensorJson&node){
    require(node.is_object()&&node.contains("Exists")&&node["Exists"].is_boolean()&&node.contains("Value")&&node["Value"].is_number_integer(),"Sensor state type unconfirmed");
    const auto& value=node["Value"];require(value>=-2147483648LL&&value<=4294967295ULL,"Sensor DWORD outside range");
    const bool exists=node["Exists"].get<bool>();require(exists||value==0,"Missing sensor has a nonzero value");
    return {exists,value.is_number_unsigned()?static_cast<DWORD>(value.get<unsigned long long>()):static_cast<DWORD>(value.get<long long>())};
}
inline SensorJson parseSensorReceipt(const std::string&bytes,const std::wstring&destination,const std::string&kind){
    require(!bytes.empty()&&bytes.size()<=16384,"Sensor receipt exceeds fixed budget");
    std::vector<std::set<std::wstring>> keys;
    const auto callback=[&](int depth,SensorJson::parse_event_t event,SensorJson&value){
        require(depth<=32,"Sensor receipt nesting exceeds budget");
        if(event==SensorJson::parse_event_t::object_start)keys.emplace_back();
        if(event==SensorJson::parse_event_t::key){const auto key=lower(sensorWide(value.get<std::string>()));require(!keys.empty()&&keys.back().insert(key).second,"Duplicate sensor receipt key");}
        if(event==SensorJson::parse_event_t::object_end)keys.pop_back();return true;
    };
    const auto record=SensorJson::parse(bytes,callback);
    require(record.is_object()&&record.contains("schema")&&record["schema"].is_number_integer()&&record["schema"]==1&&record.contains("executableSha256")&&record["executableSha256"].is_string()&&lower(sensorWide(record["executableSha256"]))==sensorWide(verifiedSensorImage)&&record.contains("values")&&record["values"].is_object(),"Sensor receipt schema/image unconfirmed");
    if(record.contains("owner"))require(record["owner"].is_string()&&record["owner"]=="YMCC.HWiNFO.SensorBudget","Sensor receipt owner changed");
    if(record.contains("iniPath")){require(record["iniPath"].is_string(),"Sensor receipt path type changed");const auto path=sensorWide(record["iniPath"]);require(std::filesystem::path(path).is_absolute()&&sensorPath(path)==sensorPath(destination),"Sensor receipt INI identity changed");}
    if(record.contains("phase")){require(kind=="last"&&record["phase"].is_string(),"Sensor receipt phase invalid");const auto phase=record["phase"].get<std::string>();require(phase=="prepared"||phase=="committed"||phase=="rolled-back"||phase=="rollback-pending","Sensor receipt phase unconfirmed");}
    std::set<std::string> names;unsigned cpu=0;bool rtss=false;
    for(auto it=record["values"].begin();it!=record["values"].end();++it){const auto name=it.key();require(sensorGroupAllowed(sensorWide(name)),"Foreign sensor group");(void)sensorState(*it);names.insert(name);if(name.rfind("F0000302_",0)==0)++cpu;if(name=="F00F5000_0")rtss=true;}
    require(kind=="original"||kind=="cstate-original"||kind=="rtss-original"||kind=="last","Unknown sensor receipt kind");
    if(kind=="last")require(cpu==0||cpu==32,"Incomplete legacy sensor family");
    std::set<std::string> expected;for(const auto&g:sensorGroups()){const auto name=ascii(g);if((kind=="original"&&name.rfind("F0000101_",0)==0)||(kind=="cstate-original"&&name.rfind("F0000302_",0)==0)||(kind=="rtss-original"&&name=="F00F5000_0")||(kind=="last"&&(name.rfind("F0000101_",0)==0||(cpu==32&&name.rfind("F0000302_",0)==0)||(rtss&&name=="F00F5000_0"))))expected.insert(name);}
    require(names==expected,"Sensor receipt inventory incomplete");return record;
}
inline bool sensorDebtPending(const SensorJson&record){return !record.contains("phase")||record["phase"]=="prepared"||record["phase"]=="rollback-pending";}
inline std::string sensorReceipt(const std::map<std::wstring,SensorState>&states,const std::wstring&prefix=L""){SensorJson values=SensorJson::object();for(const auto&[group,state]:states)if(prefix.empty()||group.compare(0,prefix.size(),prefix)==0)values[ascii(group)]={{"Exists",state.exists},{"Value",state.value}};const auto text=SensorJson({{"schema",1},{"executableSha256",verifiedSensorImage},{"values",std::move(values)}}).dump();require(text.size()<=16384,"Sensor backup exceeds budget");return text;}
// All ports below are owned by the existing recovery mutex. Detection is read
// only; unknown records cannot be reset to an empty/no-debt default.
template<class Ops> std::string sensorJournalHealth(Ops&ops){try{
    std::map<std::string,SensorJson> receipts;for(const auto*kind:{"original","cstate-original","rtss-original","last"})if(const auto bytes=ops.read(kind))receipts[kind]=parseSensorReceipt(*bytes,ops.destination,kind);
    if(!receipts.count("last"))return receipts.empty()?"confirmed":"unconfirmed-missing-last";
    const auto&last=receipts["last"];if(sensorDebtPending(last))return "unconfirmed-prior-debt";
    for(const auto&pair:{std::pair{"original","F0000101_0"},std::pair{"cstate-original","F0000302_0"},std::pair{"rtss-original","F00F5000_0"}})if(last["values"].contains(pair.second)&&!receipts.count(pair.first))return "unconfirmed-missing-original";
    return "confirmed";
}catch(...){return "unconfirmed-journal";}}

template<class Ops> std::string sensorJournalBudget(Ops&ops){
    const SensorState disabled{true,0};std::map<std::string,std::optional<std::string>> raw;std::map<std::string,SensorJson> saved;
    std::map<std::wstring,SensorState> before;std::vector<std::wstring> changed;SensorJson record;bool started=false;
    const auto write=[&](const std::string&kind,const SensorJson&value){const auto text=value.dump();(void)parseSensorReceipt(text,ops.destination,kind);ops.write(kind,text,raw[kind]);raw[kind]=text;};
    const auto phase=[&](const char*value){record["owner"]="YMCC.HWiNFO.SensorBudget";record["iniPath"]=sensorUtf8(ops.destination);record["phase"]=value;write("last",record);};
    try{
        for(const auto*kind:{"original","cstate-original","rtss-original","last"}){raw[kind]=ops.read(kind);if(raw[kind])saved[kind]=parseSensorReceipt(*raw[kind],ops.destination,kind);}
        const bool priorPending=saved.count("last")&&sensorDebtPending(saved["last"]);
        if(!saved.count("last")&&!saved.empty())return "unconfirmed-missing-last";
        if(!ops.verified())return priorPending?"unconfirmed-unsupported-image":"unsupported-image";
        if(!ops.enabled())return priorPending?"unconfirmed-preferences-disabled":"preferences-disabled";
        ops.stopped();
        if(saved.count("last")&&sensorDebtPending(saved["last"])){
            record=saved["last"];
            for(auto it=record["values"].begin();it!=record["values"].end();++it){ops.stopped();const auto current=ops.state(sensorWide(it.key())),prior=sensorState(*it);require(current==prior||current==disabled,"Foreign sensor choice; prior debt preserved");}
            for(auto it=record["values"].begin();it!=record["values"].end();++it){ops.stopped();const auto group=sensorWide(it.key());const auto prior=sensorState(*it),current=ops.state(group);if(current==prior)continue;require(current==disabled,"Concurrent sensor change during restore");ops.set(group,prior);require(ops.state(group)==prior,"Sensor restoration not confirmed");}
            phase("rolled-back");saved["last"]=record;
        }
        std::vector<std::wstring> pending;for(const auto&group:sensorGroups()){before[group]=ops.state(group);if(!(before[group]==disabled))pending.push_back(group);}
        if(pending.empty()&&!saved.count("last"))return "already-configured";
        auto first=before;if(saved.count("last"))for(auto it=saved["last"]["values"].begin();it!=saved["last"]["values"].end();++it)first[sensorWide(it.key())]=sensorState(*it);
        for(const auto&pair:{std::pair{"original",L"F0000101_"},std::pair{"cstate-original",L"F0000302_"},std::pair{"rtss-original",L"F00F5000_"}})if(!raw[pair.first])write(pair.first,SensorJson::parse(sensorReceipt(first,pair.second)));
        if(pending.empty())return "already-configured"; // Missing first files were repaired only from the proven last above.
        record=saved.count("last")?saved["last"]:SensorJson{{"schema",1},{"executableSha256",verifiedSensorImage}};auto values=SensorJson::object();for(const auto&[group,value]:before){const auto name=ascii(group);values[name]=record.contains("values")&&record["values"].contains(name)?record["values"][name]:SensorJson::object();values[name]["Exists"]=value.exists;values[name]["Value"]=value.value;}record["values"]=std::move(values);
        phase("prepared");started=true;
        for(const auto&group:pending){ops.stopped();require(ops.state(group)==before[group],"Concurrent sensor preference changed");changed.push_back(group);ops.set(group,disabled);require(ops.state(group)==disabled,"Sensor Enabled verification failed");}
        phase("committed");return "configured";
    }catch(...){bool restored=started;if(started){for(auto it=changed.rbegin();it!=changed.rend();++it)try{ops.stopped();const auto current=ops.state(*it);if(current==before[*it])continue;require(current==disabled,"Foreign sensor state preserved during rollback");ops.set(*it,before[*it]);require(ops.state(*it)==before[*it],"Sensor rollback not confirmed");}catch(...){restored=false;}try{phase(restored?"rolled-back":"rollback-pending");}catch(...){restored=false;}}
        return restored?"unconfirmed-rolled-back":"unconfirmed-rollback-deferred";
    }
}
}
