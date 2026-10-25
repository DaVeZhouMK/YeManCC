#include "../native/input_chain_command_evidence.h"
#include <iostream>
using namespace ymcc::inputChainCommand;
int main(){Json tests=Json::array();auto check=[&](const char* n,bool p){tests.push_back({{"name",n},{"passed",p}});};
Json request={{"protocolVersion",1},{"command","PREPARE_TARGET"},{"requestId","request-1"},{"sequence",12ULL},{"runId","run"},{"epoch",2ULL},{"powerGeneration",1ULL}};
Json response=request;response["ok"]=true;response["status"]="target-prepared";response["hostInstanceId"]="host";
auto bound=capture(request,"host");auto projected=project(bound,response,true,"target-prepared");
check("actual-prepare-envelope-is-verified-locally",projected["status"]=="verified-local-ack"&&projected["command"]=="PREPARE_TARGET"&&projected["protocolPowerGeneration"]==1);
check("readout-never-authorizes-game-or-Steam",projected["gameInputUnverified"]==true&&projected["steamConsumptionProven"]==false&&projected["producerScope"]=="native-lifecycle-command-receipt");
request["powerGeneration"]=8;request["epoch"]=9;check("sending-snapshot-cannot-borrow-later-current-generation",project(bound,response,true,"target-prepared")["tupleVerified"]==true&&bound.request["powerGeneration"]==1);
check("native-rejection-cannot-verify",project(bound,response,false,"target-prepared")["status"]=="not-accepted");
check("no-capture-keeps-unknown",project(Snapshot{},response,true,"target-prepared")["status"]=="unknown");
for(const char* key:{"requestId","runId","hostInstanceId","command","status"}){auto bad=response;bad[key]="foreign";std::string n="mismatch-keeps-unknown-";n+=key;check(n.c_str(),project(bound,bad,true,"target-prepared")["tupleVerified"]==false);}
for(const char* key:{"protocolVersion","powerGeneration","epoch","sequence"}){auto bad=response;bad[key]=9ULL;std::string n="numeric-mismatch-keeps-unknown-";n+=key;check(n.c_str(),project(bound,bad,true,"target-prepared")["tupleVerified"]==false);}
for(const char* key:{"powerGeneration","epoch","sequence"}){auto bad=response;bad[key]="1";std::string n="quoted-number-keeps-unknown-";n+=key;check(n.c_str(),project(bound,bad,true,"target-prepared")["tupleVerified"]==false);}
{auto bad=response;bad["ok"]=false;check("host-business-rejected-is-unverified",project(bound,bad,true,"target-prepared")["tupleVerified"]==false);}
{auto bad=response;bad["ok"]="true";check("wrong-boolean-host-acceptance-unverified",project(bound,bad,true,"target-prepared")["tupleVerified"]==false);}
{auto frame=bound.request;frame["command"]="SUBMIT_FRAME";check("frame-never-enters-command-capture",!capture(frame,"host").captured);}
{auto bad=bound.request;bad["command"]=std::string("PREPARE_TARGET\0other",20);check("embedded-NUL-command-does-not-match-lifecycle-name",!capture(bad,"host").captured);}
check("empty-host-cannot-capture",!capture(bound.request,"").captured);
{auto bad=bound.request;bad.erase("requestId");check("missing-request-field-cannot-capture",!capture(bad,"host").captured);}
{auto large=bound.request;large["powerGeneration"]=18446744073709551615ULL;large["sequence"]=9007199254740993ULL;auto reply=large;reply["ok"]=true;reply["status"]="target-prepared";reply["hostInstanceId"]="host";auto p=project(capture(large,"host"),reply,true,"target-prepared");check("full-u64-survives-projection-exactly",p["protocolPowerGeneration"]==18446744073709551615ULL&&p["wireSequence"]==9007199254740993ULL);}
for(auto pair:{std::pair{"RECREATE_TARGET","target-recreated"},std::pair{"RELEASE_TARGET","release-local-dispose-complete"},std::pair{"SHUTDOWN","shutdown-complete"}}){auto req=bound.request;req["command"]=pair.first;auto reply=req;reply["ok"]=true;reply["status"]=pair.second;reply["hostInstanceId"]="host";check(pair.first,project(capture(req,"host"),reply,true,pair.second)["tupleVerified"]==true);}
{Json row={{"command","PREPARE_TARGET"},{"powerGeneration",99}};auto receipt=response;receipt["targetCreationLifetimeEvidence"]={{"scope","fixture-only"}};append(row,bound,receipt,true,"target-prepared");check("nested-protocol-distinct-from-outer-shared-power",row["powerGeneration"]==99&&row["ackIdentity"]["protocolPowerGeneration"]==1);check("host-lifetime-is-forwarded-without-consumption-promotion",row.contains("targetCreationLifetimeEvidence")&&row["ackIdentity"]["steamConsumptionProven"]==false);}
{Json row={{"ok",true}};append(row,Snapshot{},response,true,"target-prepared");check("absent-snapshot-leaves-original-row",row==Json{{"ok",true}});}
bool pass=true;for(auto&t:tests)pass&=t["passed"].get<bool>();std::cout<<Json{{"schema","ymcc-r39-command-header-selftest-v1"},{"passed",pass},{"cases",tests.size()},{"tests",tests},{"deviceOperations",0},{"gameInput","unknown"},{"Continue","unknown"}}.dump(2)<<'\n';return pass?0:1;}
