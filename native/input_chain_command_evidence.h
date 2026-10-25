// Bounded lifecycle command ACK evidence; no input, device or repair authority.
#pragma once
#include "input_chain_ack_evidence.h"
#include <cstring>
namespace ymcc::inputChainCommand {
using Json=nlohmann::json;
inline bool supported(const char* c) noexcept {
 return c && (std::strcmp(c,"PREPARE_TARGET")==0 || std::strcmp(c,"RECREATE_TARGET")==0 || std::strcmp(c,"RELEASE_TARGET")==0 || std::strcmp(c,"SHUTDOWN")==0);
}
struct Snapshot { bool captured=false;Json request;std::string boundHost; };
// Call only on selected lifecycle commands, while the existing request mutex is held.
// The actual merged envelope, not current sampled power, is retained before pipe write.
inline Snapshot capture(const Json& request,const std::string& boundHost) noexcept {
 Snapshot s;try {
  const auto* command=inputChainAck::stringValue(request,"command");
  if(!request.is_object()||!command||(!supported(command->c_str())||command->size()!=std::strlen(command->c_str()))||boundHost.empty()||boundHost.size()>4096)return s;
  for(const char* key:{"protocolVersion","command","requestId","sequence","runId","epoch","powerGeneration"}) {
   const auto it=request.find(key);if(it==request.end())return Snapshot{};s.request[key]=*it;
  }
  s.boundHost=boundHost;s.captured=true;
 }catch(...){return Snapshot{};}return s;
}
inline Json project(const Snapshot& s,const Json& response,bool nativeAccepted,const char* expectedStatus) noexcept {
 try {
  Json out={{"schema",1},{"authority","native-local-ack-diagnostic"},{"nativeAccepted",nativeAccepted},
   {"tupleVerified",false},{"status",nativeAccepted?"unknown":"not-accepted"},{"identityOrigin","none"},
   {"gameInputUnverified",true},{"steamConsumptionProven",false},{"producerScope","native-lifecycle-command-receipt"}};
  if(!nativeAccepted||!s.captured||!response.is_object()||!expectedStatus||s.boundHost.empty())return out;
  const auto* command=inputChainAck::stringValue(s.request,"command");
  const auto* rc=inputChainAck::stringValue(response,"command");const auto* status=inputChainAck::stringValue(response,"status");
  const auto* requestId=inputChainAck::stringValue(s.request,"requestId");const auto* rId=inputChainAck::stringValue(response,"requestId");
  const auto* run=inputChainAck::stringValue(s.request,"runId");const auto* rRun=inputChainAck::stringValue(response,"runId");
  const auto* host=inputChainAck::stringValue(response,"hostInstanceId");const auto ok=response.find("ok");
  auto validText=[](const std::string* v){return v&&!v->empty()&&v->size()<=4096&&v->find_first_of("\r\n\0",0,3)==std::string::npos;};
  if(!command||(!supported(command->c_str())||command->size()!=std::strlen(command->c_str()))||!rc||*rc!=*command||!status||*status!=expectedStatus||!validText(requestId)||!rId||*rId!=*requestId||!validText(run)||!rRun||*rRun!=*run||!validText(host)||*host!=s.boundHost||ok==response.end()||!ok->is_boolean()||!ok->get<bool>())return out;
  std::uint64_t values[4]{};const char* keys[]={"protocolVersion","powerGeneration","epoch","sequence"};
  for(unsigned i=0;i<4;++i){std::uint64_t actual=0;if(!inputChainAck::unsignedValue(s.request,keys[i],values[i])||!values[i]||!inputChainAck::unsignedValue(response,keys[i],actual)||actual!=values[i])return out;}
  if(values[0]!=1)return out;
  out["status"]="verified-local-ack";out["tupleVerified"]=true;out["identityOrigin"]="existing-validator-and-parsed-dom-reply";
  out["requestBindingCapturedBeforeRequest"]=true;out["protocolPowerGeneration"]=values[1];out["hostEpoch"]=values[2];out["wireSequence"]=values[3];
  out["runId"]=*run;out["hostInstanceId"]=*host;out["requestId"]=*requestId;out["command"]=*command;
  out["protocolGenerationScope"]="input-host-bound-protocol;not-current-shared-power";return out;
 }catch(...){return Json(nullptr);}
}
inline void append(Json& row,const Snapshot& s,const Json& response,bool nativeAccepted,const char* expectedStatus) noexcept {
 try {
  if(!s.captured)return;
  row["inputChainEvidenceSchema"]=3;row["ackIdentity"]=project(s,response,nativeAccepted,expectedStatus);
  if(response.contains("targetCreationLifetimeEvidence"))row["targetCreationLifetimeEvidence"]=response["targetCreationLifetimeEvidence"];
 }catch(...){/* optional evidence loss preserves original command status */}
}
}
