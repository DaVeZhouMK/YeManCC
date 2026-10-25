// Exact Fan lease receipt header; synthetic values only, no device/network/clock.
#include <iostream>
#include <string>
#include <vector>
std::string FixtureLeaseCopy(const std::string&);
#include "LeaseHeaderExtract.h"
using json=nlohmann::json;
unsigned copies=0;size_t copiedBytes=0;
std::string FixtureLeaseCopy(const std::string& value){++copies;copiedBytes+=value.size();return value;}
unsigned checks=0;std::vector<std::string> failures;json outcomes=json::array();
void check(bool value,const std::string& name){++checks;if(!value)failures.push_back(name);}
int main(){
 const std::string id="fixture-lease-0001",other="fixture-lease-0002";
 const json lease{{"leaseId",id},{"generation",1},{"expiresAtMonoMs",15000},{"owner","fixture"}};
 auto examine=[&](const std::string& name,const json& response,const std::string& requested,bool expected){
   for(const auto* path:{"/api/heartbeat","/api/acquire-control"}){
     const bool acquire=std::string(path)=="/api/acquire-control";
     const bool accepted=ymcc::fan_response::accepted(path,200,response,requested);
     const auto selected=ymcc::fan_response::keepaliveLease(path,response,requested);
     const bool envelope=response.is_object()&&(!response.contains("ok")||(response["ok"].is_boolean()&&response["ok"].get<bool>()));
     check(accepted==(expected&&envelope),name+path+" acceptance");
     // keepaliveLease checks lease selection only; envelope is checked by accepted.
     if(envelope)check((!selected.empty())==expected,name+path+" lease selection");
     outcomes.push_back(json{{"name",name},{"path",path},{"accepted",accepted},{"selected",selected},{"acquire",acquire}});
   }
 };
 for(const auto& response:{json{{"ok",true},{"lease",lease}},json{{"result",{{"lease",lease}}}},json{{"state",{{"lease",lease}}}},json{{"lease",nullptr},{"result",{{"lease",lease}}}}})examine("valid",response,id,true);
 for(const auto& response:{json(),json::array(),json::object(),json{{"lease",nullptr}},json{{"ok",false},{"lease",lease}},json{{"ok","true"},{"lease",lease}},json{{"lease",json::object()},{"result",{{"lease",lease}}}}})examine("unusable",response,id,false);
 for(const auto& invalid:{json(nullptr),json(false),json(2),json("short"),json("fixture lease 0001"),json(std::string(81,'x')),json("fixture-lease-\xC3\xA9")}){
   auto value=lease;value["leaseId"]=invalid;examine("invalid id",json{{"lease",value}},id,false);
 }
 for(const auto& invalid:{json(nullptr),json("1"),json(0),json(-1),json(1.5),json(9007199254740992.0)}){
   auto value=lease;value["generation"]=invalid;examine("invalid generation",json{{"lease",value}},id,false);
 }
 for(const auto& invalid:{json(nullptr),json("15000"),json(0),json(-1)}){
   auto value=lease;value["expiresAtMonoMs"]=invalid;examine("invalid expiry",json{{"lease",value}},id,false);
 }
 {auto value=lease;value["owner"]=2;examine("invalid owner",json{{"lease",value}},id,false);}
 for(const auto* path:{"/api/heartbeat","/api/enable","/api/preset"}){
   const auto value=json{{"lease",lease}};check(ymcc::fan_response::keepaliveLease(path,value,other).empty(),"different request token cannot renew");
 }
 for(unsigned status:{0u,401u,403u,409u,500u})check(!ymcc::fan_response::accepted("/api/heartbeat",status,json{{"lease",lease}},id),"non-success status rejected");
 auto response=json{{"lease",lease}};const auto saved=ymcc::fan_response::keepaliveLease("/api/heartbeat",response,id);
 response["lease"]["leaseId"]=other;check(saved==id,"compatibility result must own its string");check(!ymcc::fan_response::accepted("/api/heartbeat",200,response,id),"new receipt cannot reuse old token");
#if FAN_HAS_BORROWED_LEASE
 const auto* borrowed=ymcc::fan_response::keepaliveLeaseReference("/api/heartbeat",response,other);
 check(borrowed&&borrowed==&response["lease"]["leaseId"].get_ref<const std::string&>(),"borrow points to current receipt");
 check(!ymcc::fan_response::keepaliveLeaseReference("/api/heartbeat",response,id),"borrow retains request identity check");
#endif
 copies=0;copiedBytes=0;
 for(int i=0;i<100;++i)check(ymcc::fan_response::accepted("/api/heartbeat",200,response,other),"fresh lease remains accepted");
 check(copies==0&&copiedBytes==0,"validation still creates an owned temporary lease string");
 std::cout<<json{{"ok",failures.empty()},{"checks",checks},{"failures",failures},{"outcomes",outcomes},{"validationTextCopies",copies},{"validationCopiedBytes",copiedBytes},{"hardwareCalls",0},{"networkCalls",0},{"cpuBenchmark",false}}.dump()<<'\n';return failures.empty()?0:1;
}
