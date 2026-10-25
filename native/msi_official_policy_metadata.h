#pragma once
// MSI-only, read-only diagnostic. No setters, HID/RPC, services or mode decisions.
#include <cstdint>
#include <string>
#include <vector>
#include <array>
#include <limits>
#include "json.hpp"
#ifndef YMCC_MSI_POLICY_METADATA_FAKE
#include <windows.h>
#endif
namespace ymcc::msi::policyMetadata {
using Json=nlohmann::json;
enum class View { View32, View64 };
enum class State { Value, KeyMissing, ValueMissing, Unknown, TooLarge, ChangedDuringRead };
struct Field {const wchar_t* subkey;const wchar_t* name;const char* role;};
inline constexpr std::array<Field,4> fields{{
 {L"SOFTWARE\\MSI\\MSI Center M\\Component\\User Scenario",L"AutoSwitchMode","ai-auto-policy"},
 {L"SOFTWARE\\MSI\\MSI Center M\\OsdEditor",L"ControlModeUserSet","user-preference"},
 {L"SOFTWARE\\MSI\\MSI Center M\\Component\\User Scenario",L"ControlModeUserSet","ai-temporary-return-preference"},
 {L"SOFTWARE\\MSI\\MSI Center M\\OsdEditor",L"ControlModeUpdated","official-mode-cache-not-firmware-proof"}
}};
struct Read {State state=State::Unknown;uint32_t type=0,error=0;std::vector<uint8_t> bytes;};
struct Reader {
 virtual ~Reader()=default;
 virtual uint64_t now()=0;
 virtual Read read(View,const Field&)=0;
};
inline const char* stateName(State s){switch(s){case State::Value:return "value";case State::KeyMissing:return "key-missing";case State::ValueMissing:return "value-missing";case State::TooLarge:return "too-large";case State::ChangedDuringRead:return "changed-during-read";default:return "unknown";}}
inline std::string addressText(const wchar_t* s){std::string out;while(*s){if(*s<32||*s>126)return {};out.push_back(static_cast<char>(*s++));}return out;}
inline Json exactText(const Read& r){
 if(r.state!=State::Value||r.type!=1||r.bytes.size()<2||r.bytes.size()>1024||r.bytes.size()%2||
    r.bytes.back()!=0||r.bytes[r.bytes.size()-2]!=0)return nullptr;
 std::string out;
 for(size_t i=0;i+2<r.bytes.size();i+=2){
  const uint16_t c=uint16_t(r.bytes[i])|(uint16_t(r.bytes[i+1])<<8);
  if(c==0||c<32||c>126)return nullptr;out.push_back(static_cast<char>(c));
 }
 return out;
}
inline Json snapshot(Reader& reader,uint64_t maxMs=40){
 const auto begin=reader.now();Json rows=Json::array();bool complete=true;unsigned readAttempts=0;
 for(View view:{View::View32,View::View64})for(const auto& field:fields){
  Json row={{"view",view==View::View32?32:64},{"subkey",addressText(field.subkey)},
   {"name",addressText(field.name)},{"role",field.role},{"hardwareModeVerified",false},{"writerProven",false},{"readAttempted",false}};
  const auto tick=reader.now();
  if(tick<begin||tick-begin>maxMs){row["state"]="not-read-soft-budget";complete=false;}
  else{
   try {++readAttempts;row["readAttempted"]=true;const auto value=reader.read(view,field);row["state"]=stateName(value.state);row["type"]=value.type;row["error"]=value.error;
    if(value.bytes.size()<=1024)row["rawBytes"]=value.bytes;else{row["state"]="too-large";complete=false;}
    row["exactRegSzText"]=exactText(value);row["exactRegSzTextAvailable"]=!row["exactRegSzText"].is_null();
    bool recognized=false;
    if(!row["exactRegSzText"].is_null()){
        const auto text=row["exactRegSzText"].get<std::string>();
        if(std::string(field.role)=="ai-auto-policy")recognized=text=="0"||text=="1";
        else if(std::string(field.role)=="ai-temporary-return-preference")recognized=text.empty()||text=="XInput"||text=="Desktop";
        else recognized=text=="XInput"||text=="Desktop";
    }
    row["valueRecognizedForRole"]=recognized;
    if(value.state==State::Unknown||value.state==State::TooLarge||value.state==State::ChangedDuringRead)complete=false;
   }catch(...){row["state"]="unknown-exception";complete=false;}
  }
  rows.push_back(std::move(row));
 }
 return {{"schema",1},{"beginTick",begin},{"endTick",reader.now()},{"complete",complete},
  {"fields",rows},{"readAttempts",readAttempts},{"effectiveAiConsumerView",32},{"registryMutated",false},
  {"installedComponentVersionBound",false},{"loadedAiEngineIdentityVerified",false},
  {"firmwareModeVerified",false},{"modeWriterProven",false},
  {"scope","MSI official config/cache inventory only; no settings/hardware writes or mode authority"},
  {"budgetMeaning","soft budget between reads; no hard cancellation of in-flight registry API"}};
}
#ifndef YMCC_MSI_POLICY_METADATA_FAKE
class WindowsReader final:public Reader {
public:
 uint64_t now()override{return GetTickCount64();}
 Read read(View view,const Field& f)override{
  HKEY key=nullptr;Read out;
  const REGSAM flags=KEY_QUERY_VALUE|(view==View::View32?KEY_WOW64_32KEY:KEY_WOW64_64KEY);
  const auto opened=RegOpenKeyExW(HKEY_LOCAL_MACHINE,f.subkey,0,flags,&key);
  if(opened!=ERROR_SUCCESS){out.error=opened;out.state=opened==ERROR_FILE_NOT_FOUND?State::KeyMissing:State::Unknown;return out;}
  struct Close {HKEY key;~Close(){RegCloseKey(key);}} close{key};
  DWORD type=0,size=0;const auto queried=RegQueryValueExW(key,f.name,nullptr,&type,nullptr,&size);
  out.type=type;out.error=queried;
  if(queried!=ERROR_SUCCESS){out.state=queried==ERROR_FILE_NOT_FOUND?State::ValueMissing:State::Unknown;return out;}
  if(size>1024){out.state=State::TooLarge;return out;}
  std::vector<uint8_t> bytes(size);DWORD readType=0,readSize=size;
  const auto fetched=RegQueryValueExW(key,f.name,nullptr,&readType,bytes.empty()?nullptr:bytes.data(),&readSize);
  out.error=fetched;
  if(fetched!=ERROR_SUCCESS||readType!=type||readSize!=size){out.state=State::ChangedDuringRead;return out;}
  out.state=State::Value;out.bytes=std::move(bytes);return out;
 }
};
inline Json windowsSnapshot(uint64_t maxMs=40){WindowsReader reader;return snapshot(reader,maxMs);}
#endif
} // namespace ymcc::msi::policyMetadata
