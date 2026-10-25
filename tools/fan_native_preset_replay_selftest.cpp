// Actual Fan activity observer/snapshot; inert persistence and lifecycle sinks.
#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "fan_response_contract.h"
std::string FixtureActivityIdentityTextCopy(const nlohmann::json&);
const std::string& FixtureActivityIdentityTextReference(const nlohmann::json&);
std::string FixtureActivityIdentityOwnedCopy(const std::string&);
#include "fan_activity_identity.h"
#if __has_include("fan_replay_curve.h")
#include "fan_replay_curve.h"
#endif
using json=nlohmann::json;using DWORD=unsigned long;using ULONGLONG=unsigned long long;
unsigned identityTextCopies=0,identityTextReads=0;size_t identityBytesCopied=0;
std::string FixtureActivityIdentityTextCopy(const json& value){++identityTextCopies;++identityTextReads;identityBytesCopied+=value.get_ref<const std::string&>().size();return value.get<std::string>();}
const std::string& FixtureActivityIdentityTextReference(const json& value){++identityTextReads;return value.get_ref<const std::string&>();}
std::string FixtureActivityIdentityOwnedCopy(const std::string& value){++identityTextCopies;identityBytesCopied+=value.size();return value;}

struct Activity{bool active=true;int revision=1;json curve=json::array();std::string deviceClass="HandheldCompanion.Devices.GPDWin5",runToken="fixture-run",bootTag="fixture-boot",packageId="fixture-package";}g_fanActivity;
std::mutex g_fanActivityMx;std::atomic<bool>g_fanActivityIntentActive{true};bool g_fanActivityRecoverable=false;std::string g_fanCurrentRunToken="fixture-run";
struct Display{ULONGLONG current=1;ULONGLONG epoch()const{return current;}}g_monitorFanDisplay;
unsigned writes=0,checks=0;json persisted;std::vector<std::string>failures;
std::string fanActivityBootTag(){return"fixture-boot";}std::string fanActivityCurrentPackageId(){return"fixture-package";}
void fanActivityPersistLocked(){++writes;persisted=json{{"active",g_fanActivity.active},{"revision",g_fanActivity.revision},{"curve",g_fanActivity.curve},{"deviceClass",g_fanActivity.deviceClass}};}
void appendFanLifecycleLog(const char*,const json&){}
#include "ActivityExtract.inc"
#include "SnapshotExtract.inc"
json curve(std::initializer_list<double> duties){json value=json::array();int i=0;for(double duty:duties){value.push_back(json{{"tempC",i==0?0:i==1?40:i==2?70:100},{"dutyPercent",duty}});++i;}return value;}
json reply(const json& nodes){return json{{"ok",true},{"state",{{"state","Ready"},{"protocolVersion","2"},{"factoryType",g_fanActivity.deviceClass},{"activeCurve",nodes},{"hardwareWritesEnabled",true},{"openCalled",true},{"openEventsCalled",true},{"lease",{{"leaseId","fixture-lease-0001"},{"generation",1}}}}}};}
void expect(bool value,const char* text){++checks;if(!value)failures.push_back(text);}
void observe(const json& request,const json& response,const std::string& path="/api/preset",DWORD status=200,ULONGLONG epoch=1){fanActivityObserve(path,request.dump(),status,response.dump(),&response,epoch);}
int main(int argc,char**argv){if(argc!=2)return 2;const std::string scenario=argv[1];const auto old=curve({100,100,100,100}),balanced=curve({0,20,45,90}),manual=curve({85,85,85,85});g_fanActivity.curve=old;
try{
 if(scenario=="named"){
  for(const auto* family:{"ROGAlly","MSI.ClawCG3EM","GPDWin5","OneXPlayerX1Mini","Lenovo.LegionGo"})for(const auto& nodes:{curve({0,0,20,80}),balanced,curve({0,30,75,100})}){
   g_fanActivity.deviceClass=std::string("HandheldCompanion.Devices.")+family;g_fanActivity.curve=old;observe(json{{"name","fixture-named"}},reply(nodes));expect(g_fanActivity.curve==nodes,"committed named preset kept the older curve");expect(persisted["curve"]==nodes,"persisted crash intent differs from current preset");expect(fanActivitySnapshotJson()["curve"]==nodes,"native wake snapshot differs from current preset");
  }
 }else if(scenario=="explicit"){
  observe(json{{"name","balanced"},{"nodes",manual}},reply(balanced));expect(g_fanActivity.curve==manual,"explicit caller curve lost precedence");expect(writes==1&&g_fanActivityIntentActive,"manual control was not retained");
 }else if(scenario=="missing"){
  auto response=reply(balanced);response["state"].erase("activeCurve");observe(json{{"name","balanced"}},response);expect(g_fanActivity.curve.empty(),"missing preset receipt retained old replay data");expect(g_fanActivity.active&&g_fanActivityIntentActive,"missing replay curve revoked current control intent");expect(persisted["curve"].empty(),"missing curve persisted an older preset");
 }else if(scenario=="invalid"){
  const std::vector<json> invalid={json(),json::array(),json::array({nullptr}),json::array({json{{"tempC",1},{"dutyPercent",0}},balanced[1],balanced[2],balanced[3]}),json::array({balanced[0],balanced[1],json{{"tempC",10},{"dutyPercent",45}},balanced[3]}),json::array({balanced[0],balanced[1],balanced[2],json{{"tempC",100},{"dutyPercent",40}}}),json::array({balanced[0],balanced[1],json{{"tempC",70},{"dutyPercent",101}},balanced[3]}),json::array({balanced[0],balanced[1],json{{"tempC","70"},{"dutyPercent",45}},balanced[3]})};
  for(const auto& nodes:invalid){g_fanActivity.curve=old;observe(json{{"name","balanced"}},reply(nodes));expect(g_fanActivity.curve.empty(),"invalid preset receipt retained old replay data");expect(g_fanActivityIntentActive,"invalid replay metadata blocked the committed intent");}
 }else if(scenario=="stale"){
  g_monitorFanDisplay.current=2;observe(json{{"name","balanced"}},reply(balanced));expect(g_fanActivity.curve==old&&writes==0,"late old preset changed current record");
 }else if(scenario=="failed"){
  observe(json{{"name","balanced"}},reply(balanced),"/api/preset",500);expect(g_fanActivity.curve==old&&writes==0,"unknown failed write was invented as a new confirmed preset");
 }else if(scenario=="idle-cost"){
  for(int i=0;i<100;++i){observe(json::object(),reply(old),"/api/state");observe(json::object(),reply(old),"/api/heartbeat");}expect(writes==0&&g_fanActivity.curve==old,"read/heartbeat began persisting or changing curves");
 }else if(scenario=="identity-cost"){
  for(int i=0;i<100;++i)observe(json::object(),reply(old),"/api/state");expect(identityTextCopies==0&&identityBytesCopied==0,"state reads still copied transient identity text");expect(identityTextReads==100,"identity freshness was cached instead of reading every new receipt");expect(writes==0&&g_fanActivity.deviceClass=="HandheldCompanion.Devices.GPDWin5","steady read changed persistence or identity");
 }else if(scenario=="identity-change"){
  auto response=reply(old);response["state"]["factoryType"]="HandheldCompanion.Devices.MSI.ClawCG3EM";observe(json::object(),response,"/api/state");expect(g_fanActivity.deviceClass=="HandheldCompanion.Devices.MSI.ClawCG3EM","new current identity was cached away");response["state"]["factoryType"]="HandheldCompanion.Devices.ROGAlly";expect(g_fanActivity.deviceClass=="HandheldCompanion.Devices.MSI.ClawCG3EM","persistent identity aliases a destroyed/changed reply");observe(json::object(),response,"/api/state");expect(g_fanActivity.deviceClass=="HandheldCompanion.Devices.ROGAlly","changed reply identity was not refreshed");response["deviceClass"]="HandheldCompanion.Devices.GPDWin5";response["state"]["factoryType"]="HandheldCompanion.Devices.OneXPlayerX1Mini";observe(json::object(),response,"/api/state");expect(g_fanActivity.deviceClass=="HandheldCompanion.Devices.ROGAlly","conflicting identities overwrote the previous valid value");expect(writes==0,"identity state observation began writing disk");
 }else if(scenario=="revoke"){
  observe(json{{"name","balanced"}},reply(balanced));observe(json::object(),reply(balanced),"/api/disable");expect(!g_fanActivityIntentActive&&!g_fanActivity.active,"new preset ignored explicit OFF");expect(writes==2,"OFF persistence responsibility changed");
 }else throw std::runtime_error("unknown scenario");
 std::cout<<json{{"ok",failures.empty()},{"scenario",scenario},{"checks",checks},{"failures",failures},{"record",fanActivitySnapshotJson()},{"persisted",persisted},{"persistenceWrites",writes},{"identityTextCopies",identityTextCopies},{"identityTextReads",identityTextReads},{"identityBytesCopied",identityBytesCopied},{"hardwareCalls",0},{"cpuBenchmark",false}}.dump()<<'\n';return failures.empty()?0:1;
}catch(const std::exception&error){std::cout<<json{{"ok",false},{"fixtureError",error.what()},{"scenario",scenario}}.dump()<<'\n';return 2;}}
