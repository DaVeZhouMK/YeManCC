// Actual Fan Arm/Note/Mode/Tick and actual renderer-send hook; inert transport/power.
#include <atomic>
#include <algorithm>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include "fan_response_contract.h"
using json=nlohmann::json;using DWORD=unsigned long;using ULONGLONG=unsigned long long;using HANDLE=void*;
std::atomic<bool> g_fanLeaseKeepaliveArmed{true},g_fanLeaseKeepaliveStop{false},g_exitRequested{false},g_fanSleepCleanupInFlight{false},g_fanLeaseKeepaliveInFlight{false},g_recoveryFanEverOwned{false};
std::atomic<ULONGLONG> g_fanLeaseKeepaliveLastRenewTick{0},g_fanMsiModeRevision{0};
std::atomic<unsigned long long> g_fanMsiModePowerGeneration{0},g_fanKeepaliveRendererEpoch{1};
std::mutex g_fanLeaseKeepaliveMx;std::string g_fanLeaseId="fixture-lease-0001";
unsigned long long g_fanKeepaliveRendererEpochSnapshot=1,sharedGeneration=7,fanGeneration=7;
ULONGLONG g_fanKeepaliveRendererGraceUntil=0,fixtureClock=100000;constexpr ULONGLONG FAN_LEASE_RENDERER_GRACE_MS=90000;constexpr DWORD FAN_LEASE_KEEPALIVE_INTERVAL_MS=5000;
HANDLE g_fanLeaseKeepaliveWake=nullptr;bool mayWrite=true;unsigned rendererPosts=0,nativePosts=0,ensureCalls=0,wakeCalls=0,checks=0;DWORD lastStatus=0;
bool modelLeaseExpiry=false,modelExpired=false;ULONGLONG modelExpiresAt=0;unsigned controlReceipts=0;std::vector<ULONGLONG> renewalTimes;
std::vector<json> logs;std::vector<unsigned long long> receivedRevisions;std::vector<std::string> failures,wireBodies;std::string lastWireBody;
struct DisplayEpoch{std::atomic<ULONGLONG> value{1};ULONGLONG epoch()const{return value.load();}ULONGLONG invalidate(){return value.fetch_add(1)+1;}}g_monitorFanDisplay;
ULONGLONG GetTickCount64(){return fixtureClock;}
unsigned long long currentPowerGeneration(){return sharedGeneration;}
unsigned long long currentFanPowerGeneration(){return fanGeneration;}
bool fanHostPowerMayWrite(){return mayWrite;}
bool fanLeaseKeepaliveMayRun(){return mayWrite&&!g_exitRequested&&!g_fanLeaseKeepaliveStop&&!g_fanSleepCleanupInFlight;}
bool isFanLeaseIdValid(const std::string& s){return ymcc::fan_response::leaseIdValid(s);}
std::string ascii_lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return s;}
void appendFanLifecycleLog(const char* name,const json& data){logs.push_back(json{{"event",name},{"details",data}});}
void appendNativeLifecycleLog(const char*,const json&){}
bool SetEvent(HANDLE){++wakeCalls;return true;}
void fanLeaseKeepaliveEnsureThread(){++ensureCalls;}
std::function<void()> armUnlockHook;
struct FixtureArmLock {std::mutex& target;explicit FixtureArmLock(std::mutex& value):target(value){target.lock();}~FixtureArmLock(){target.unlock();if(armUnlockHook){auto hook=std::move(armUnlockHook);armUnlockHook=nullptr;hook();}}};
static void fanLeaseKeepaliveDisarm(const char* reason,const std::string* expectedLease=nullptr);
#include "ArmExtract.inc"
#include "DisarmExtract.inc"
#include "NoteExtract.inc"
#include "ModeExtract.inc"
unsigned metadataParseCalls=0,metadataDumpCalls=0,canonicalBuildCalls=0;bool throwMetadataParse=false,throwMetadataDump=false,throwCanonicalBuild=false;std::function<void()> metadataDumpHook;
json FixtureRendererParse(const std::string& body){++metadataParseCalls;if(throwMetadataParse){throwMetadataParse=false;throw std::bad_alloc();}return json::parse(body,nullptr,false);}
std::string FixtureRendererDump(const json& payload){++metadataDumpCalls;if(throwMetadataDump){throwMetadataDump=false;throw std::bad_alloc();}auto text=payload.dump();if(metadataDumpHook){auto hook=std::move(metadataDumpHook);hook();}return text;}
#include "FixtureConfig.inc"
#if FAN_FIXTURE_CANONICAL_BUILDER
#include "RendererModeBodyExtract.inc"
std::string FixtureRendererCanonicalBody(const std::string& body,ULONGLONG revision){++canonicalBuildCalls;if(throwCanonicalBuild){throwCanonicalBuild=false;throw std::bad_alloc();}auto text=fanLeaseKeepaliveRendererModeBody(body,revision);if(metadataDumpHook){auto hook=std::move(metadataDumpHook);hook();}return text;}
#endif
#include "RendererModeExtract.inc"
std::string sessionValue(const json& headers){if(!headers.is_object())return{};for(const auto& item:headers.items())if(ascii_lower(item.key())=="x-yeman-fan-session"&&item.value().is_string())return item.value().get<std::string>();return{};}
DWORD serve(const std::string& body,const json& headers,bool target=true,const std::string& method="POST",const std::string& path="/api/heartbeat"){
 if(!target||ascii_lower(method)!="post"||path!="/api/heartbeat")return 404;
 if(sessionValue(headers)!="fixture-authorized-session")return 401;
 auto payload=json::parse(body,nullptr,false);if(!payload.is_object())return 400;
 if(!payload.contains("leaseId")||!payload["leaseId"].is_string()||payload["leaseId"]!=g_fanLeaseId||g_fanLeaseId.empty())return 409;
 if(!mayWrite)return 409;
 if(modelLeaseExpiry){if(fixtureClock>=modelExpiresAt){modelExpired=true;return 409;}modelExpiresAt=fixtureClock+FAN_FIXTURE_LEASE_TTL_MS;renewalTimes.push_back(fixtureClock);}
 if(payload.contains("msiFirmwareModeRevision")&&payload["msiFirmwareModeRevision"].is_number_integer()){
  const auto revision=payload["msiFirmwareModeRevision"].get<long long>();if(revision>0)receivedRevisions.push_back(static_cast<unsigned long long>(revision));
 }
 return 200;
}
struct FakeTransport{std::string body;void send(const std::string& value){body=value;}};
bool fanLeaseKeepalivePostHeartbeat(const std::string& leaseId,std::string* renewed,DWORD* status,std::string* code){
 ++nativePosts;FakeTransport transport;
 #include "NativePayloadExtract.inc"
 *status=serve(transport.body,json{{"X-YeMan-Fan-Session","fixture-authorized-session"}});*code=*status==200?"":"LEASE_INVALID";*renewed=*status==200?leaseId:std::string{};return *status==200;
}
#include "TickExtract.inc"
struct FakeRequest{const json& headers;bool target;const std::string& method;const std::string& path;void send(const std::string& body){++rendererPosts;lastWireBody=body;wireBodies.push_back(body);lastStatus=serve(body,headers,target,method,path);}};
json headers(){return json{{"Content-Type","application/json"},{"X-YeMan-Fan-Session","fixture-authorized-session"}};}
std::string validBody(){return json{{"leaseId",g_fanLeaseId}}.dump();}
void renderer(std::string body,const json& hdrs=headers(),bool monitorFanRequest=true,const std::string& method="POST",const std::string& monitorFanPath="/api/heartbeat",std::optional<ULONGLONG> epoch=std::nullopt){
 const auto monitorFanEpoch=epoch.value_or(g_monitorFanDisplay.epoch());FakeRequest request{hdrs,monitorFanRequest,method,monitorFanPath};
 #include "RendererSendExtract.inc"
 if(lastStatus==200)fanLeaseKeepaliveArm(g_fanLeaseId,"/api/heartbeat",monitorFanEpoch);
}
void expect(bool value,const char* message){++checks;if(!value)failures.push_back(message);}
void note(){fanHostNoteMsiModeWrite(sharedGeneration);}
int main(int argc,char** argv){
 if(argc!=2)return 9;const std::string scenario=argv[1];
 try{
  fanLeaseKeepaliveArm(g_fanLeaseId,"/api/acquire-control");
  if(scenario=="lease-clock-ownership"){
    const auto oldLease=g_fanLeaseId;
    armUnlockHook=[](){fixtureClock+=1000;fanLeaseKeepaliveDisarm("fixture-owner-switch");fanLeaseKeepaliveArm("new-current-lease","/api/acquire-control",g_monitorFanDisplay.epoch());fixtureClock+=500;};
    fixtureClock+=1000;fanLeaseKeepaliveArm(oldLease,"/api/heartbeat",g_monitorFanDisplay.epoch());
    expect(g_fanLeaseId=="new-current-lease","old receipt replaced the newer exact candidate");
    expect(g_fanLeaseKeepaliveLastRenewTick==fixtureClock-500,"old receipt retimed the new owner after unlocking its original boundary");
   }else if(scenario=="lease-clock-enable"||scenario=="lease-clock-preset"||scenario=="lease-clock-mixed"){
    modelLeaseExpiry=true;modelExpiresAt=fixtureClock+FAN_FIXTURE_LEASE_TTL_MS;const auto begin=fixtureClock;
    for(unsigned elapsed=1000;elapsed<=30000;elapsed+=1000){
     fixtureClock=begin+elapsed;if(fixtureClock>=modelExpiresAt)modelExpired=true;
     if(!modelExpired){const auto* path=scenario=="lease-clock-enable"?"/api/enable":scenario=="lease-clock-preset"?"/api/preset":elapsed%2000==0?"/api/preset":"/api/enable";fanLeaseKeepaliveArm(g_fanLeaseId,path,g_monitorFanDisplay.epoch());++controlReceipts;}
     if(elapsed%5000==0)fanLeaseKeepaliveTick();
    }
    expect(!modelExpired,"valid curve/preset receipts postponed native renewal until the modeled 15s lease expired");
    expect(nativePosts==6&&renewalTimes.size()==6,"native backup must still renew on its existing five-second ticks");
    expect(controlReceipts==30&&g_fanLeaseKeepaliveArmed,"ongoing control must remain reachable without reacquire/another process");
    expect(rendererPosts==0,"the fallback proof accidentally used renderer heartbeats");
   }else if(scenario=="lease-clock-evidence"){
    const auto begin=fixtureClock;
    for(const auto* path:{"/api/enable","/api/preset","native.power-resume","unknown-origin"}){fixtureClock+=1000;fanLeaseKeepaliveArm(g_fanLeaseId,path,g_monitorFanDisplay.epoch());expect(g_fanLeaseKeepaliveLastRenewTick==begin,"a retained lease clock trusted a non-renewing/unknown receipt");}
    fixtureClock+=1000;fanLeaseKeepaliveArm(g_fanLeaseId,"/api/heartbeat",g_monitorFanDisplay.epoch());expect(g_fanLeaseKeepaliveLastRenewTick==fixtureClock,"real heartbeat renewal did not refresh its clock");
    fixtureClock+=1000;fanLeaseKeepaliveArm(g_fanLeaseId,"/api/acquire-control",g_monitorFanDisplay.epoch());expect(g_fanLeaseKeepaliveLastRenewTick==fixtureClock,"real acquisition did not refresh its clock");
    fixtureClock+=1000;fanLeaseKeepaliveArm("new-current-lease","native.power-resume",g_monitorFanDisplay.epoch());expect(g_fanLeaseId=="new-current-lease"&&g_fanLeaseKeepaliveLastRenewTick==fixtureClock,"new exact candidate failed to initialize its existing schedule");
    fanLeaseKeepaliveDisarm("fixture-off");fixtureClock+=1000;fanLeaseKeepaliveArm("new-manual-lease","/api/enable",g_monitorFanDisplay.epoch());expect(g_fanLeaseKeepaliveArmed&&g_fanLeaseKeepaliveLastRenewTick==fixtureClock,"first manual candidate did not initialize after OFF");
   }else if(scenario=="lease-clock-refused-boundaries"){
    const auto begin=fixtureClock;const auto epoch=g_monitorFanDisplay.epoch();
    fixtureClock+=1000;g_monitorFanDisplay.invalidate();fanLeaseKeepaliveArm(g_fanLeaseId,"/api/heartbeat",epoch);expect(g_fanLeaseKeepaliveLastRenewTick==begin,"old epoch changed renewal time");
    mayWrite=false;fixtureClock+=1000;fanLeaseKeepaliveArm(g_fanLeaseId,"/api/heartbeat",g_monitorFanDisplay.epoch());expect(g_fanLeaseKeepaliveLastRenewTick==begin,"sleep/denied native power changed renewal time");
    mayWrite=true;fixtureClock+=1000;fanLeaseKeepaliveArm("!","/api/heartbeat",g_monitorFanDisplay.epoch());expect(g_fanLeaseKeepaliveLastRenewTick==begin,"invalid lease changed renewal time");
    fanLeaseKeepaliveArm(g_fanLeaseId,"/api/heartbeat",g_monitorFanDisplay.epoch());expect(g_fanLeaseKeepaliveLastRenewTick==fixtureClock,"fresh authorized manual candidate remained blocked after the refused boundaries");
   }else if(scenario=="renderer-starvation"){
   note();for(int i=0;i<5;++i){fixtureClock+=5000;renderer(validBody());fanLeaseKeepaliveTick();}
   expect(rendererPosts==5&&nativePosts==0,"aligned renderer receipts did not keep native Tick on its existing throttle");expect(receivedRevisions.size()==5&&std::all_of(receivedRevisions.begin(),receivedRevisions.end(),[](auto r){return r==1;}),"known mode edge starved behind healthy renderer renewals");expect(g_fanLeaseKeepaliveArmed&&g_fanLeaseKeepaliveLastRenewTick==fixtureClock,"mode delivery changed current lease maintenance");
  }else if(scenario=="duplicate-for-deferred"){
   note();fixtureClock+=5000;renderer(validBody());fixtureClock+=5000;renderer(validBody());expect(receivedRevisions==std::vector<unsigned long long>{1,1},"same pending revision did not reach the next existing heartbeat for Host deferred rearm");expect(nativePosts==0&&rendererPosts==2,"deferred notification added native requests");
  }else if(scenario=="native-backup"){
   note();fixtureClock+=5000;fanLeaseKeepaliveTick();expect(nativePosts==1&&receivedRevisions==std::vector<unsigned long long>{1},"native fallback lost its existing mode payload");
  }else if(scenario=="zero-edge"){
   const auto body=std::string(" { \"leaseId\" : \"fixture-lease-0001\" } ");renderer(body);expect(lastWireBody==body&&receivedRevisions.empty(),"no-edge heartbeat rewrote request data");expect(metadataParseCalls==0&&metadataDumpCalls==0,"no-edge heartbeat added JSON work");
  }else if(scenario=="multiple-edges"){
   note();note();note();renderer(validBody());expect(receivedRevisions==std::vector<unsigned long long>{3},"coalesced heartbeat did not carry the latest real edge");expect(g_fanMsiModeRevision==3&&rendererPosts==1,"payload invented edges or extra requests");
  }else if(scenario=="authorization"){
   note();const auto body=validBody();renderer(body,json::object());expect(lastStatus==401&&lastWireBody==body&&receivedRevisions.empty(),"missing credentials were amended or authorized");renderer(body,json{{"X-YeMan-Fan-Session","wrong-fixture-session"}});expect(lastStatus==401&&receivedRevisions.empty(),"wrong credentials reached mode consumer");renderer(body);expect(lastStatus==200,"corrected manual heartbeat rejected");expect(receivedRevisions==std::vector<unsigned long long>{1},"corrected request omitted pending mode metadata");
  }else if(scenario=="lease-boundary"){
   note();const auto other=json{{"leaseId","foreign-fixture-lease"}}.dump();renderer(other);expect(lastStatus==409&&lastWireBody==other&&receivedRevisions.empty(),"foreign lease borrowed native mode evidence");renderer(validBody());expect(lastStatus==200,"current lease heartbeat rejected");expect(receivedRevisions==std::vector<unsigned long long>{1},"current lease omitted mode metadata after refusal");
  }else if(scenario=="malformed"){
   note();const std::string bad="{not-json}";renderer(bad);expect(lastStatus==400&&lastWireBody==bad,"optional metadata changed malformed-request classification");renderer(validBody());expect(lastStatus==200,"next valid manual request rejected");expect(receivedRevisions==std::vector<unsigned long long>{1},"next valid request omitted mode metadata");
  }else if(scenario=="existing-field"){
   note();const auto explicitBody=json{{"leaseId",g_fanLeaseId},{"msiFirmwareModeRevision",0}}.dump();renderer(explicitBody);expect(lastWireBody==explicitBody,"metadata overwrote a caller-owned field");renderer(validBody());expect(receivedRevisions==std::vector<unsigned long long>{1},"next ordinary heartbeat lost the native field");
  }else if(scenario=="stale-power"){
   note();++fanGeneration;const auto body=validBody();renderer(body);expect(lastWireBody==body&&receivedRevisions.empty(),"old private power cycle contributed mode evidence");note();renderer(validBody());expect(receivedRevisions==std::vector<unsigned long long>{2},"current private Fan cycle lost its real mapped mode edge");
  }else if(scenario=="stale-epoch"){
   note();const auto before=g_monitorFanDisplay.epoch();g_monitorFanDisplay.invalidate();const auto body=validBody();renderer(body,headers(),true,"POST","/api/heartbeat",before);expect(lastWireBody==body&&receivedRevisions.empty(),"retired request epoch gained new native metadata");renderer(body);expect(receivedRevisions==std::vector<unsigned long long>{1},"current request could not carry the pending edge");
  }else if(scenario=="off"){
   note();const auto body=validBody();fanLeaseKeepaliveDisarm("fixture-off");renderer(body);fanLeaseKeepaliveTick();expect(lastWireBody==body&&receivedRevisions.empty()&&nativePosts==0,"OFF gained mode replay or native requests");fanLeaseKeepaliveArm("fixture-next-manual-lease","fixture-manual");renderer(validBody());expect(lastStatus==200,"fresh manual candidate rejected after OFF");expect(receivedRevisions==std::vector<unsigned long long>{1},"fresh manual candidate omitted pending mode metadata");
  }else if(scenario=="sleep"){
   note();mayWrite=false;const auto body=validBody();renderer(body);fanLeaseKeepaliveTick();expect(lastWireBody==body&&lastStatus==409&&receivedRevisions.empty()&&nativePosts==0,"sleep acquired mode delivery or a new heartbeat");mayWrite=true;++fanGeneration;note();renderer(validBody());expect(lastStatus==200,"current awake manual request rejected");expect(receivedRevisions==std::vector<unsigned long long>{2},"current awake request omitted pending mode metadata");
  }else if(scenario=="large-body"){
   note();const auto body=json{{"leaseId",g_fanLeaseId},{"padding",std::string(65537,'x')}}.dump();renderer(body);expect(lastWireBody==body,"oversized optional metadata work rewrote a request");
  }else if(scenario=="routing"){
   note();const auto body=validBody();renderer(body,headers(),false);expect(lastWireBody==body,"non-Fan target changed");for(const auto* method:{"GET","HEAD"}){renderer(body,headers(),true,method);expect(lastWireBody==body,"read-only method changed");}for(const auto* path:{"/api/enable","/api/close","/other"}){renderer(body,headers(),true,"POST",path);expect(lastWireBody==body,"non-heartbeat path changed");}renderer(body,headers(),true,"Post");expect(lastStatus==200,"existing case-insensitive POST rejected");expect(receivedRevisions==std::vector<unsigned long long>{1},"existing case-insensitive POST lost Fan metadata");
  }else if(scenario=="header-casing"){
   note();renderer(validBody(),json{{"x-yEmAn-fAn-SeSsIoN","fixture-authorized-session"}});expect(lastStatus==200,"case-insensitive credential rejected");expect(receivedRevisions==std::vector<unsigned long long>{1},"case-insensitive session header lost mode metadata");
  }else if(scenario=="stop-flags"){
   note();const auto body=validBody();g_fanLeaseKeepaliveStop=true;renderer(body);expect(lastWireBody==body,"stopped native owner added metadata");g_fanLeaseKeepaliveStop=false;g_exitRequested=true;renderer(body);expect(lastWireBody==body,"exiting owner added metadata");g_exitRequested=false;g_fanSleepCleanupInFlight=true;renderer(body);expect(lastWireBody==body,"sleep cleanup added metadata");g_fanSleepCleanupInFlight=false;renderer(body);expect(receivedRevisions==std::vector<unsigned long long>{1},"eligible current owner omitted metadata");
  }else if(scenario=="metadata-failure"){
   note();const auto body=std::string(" ")+validBody();throwMetadataParse=true;renderer(body);expect(lastStatus==200&&lastWireBody==body&&receivedRevisions.empty(),"optional parse failure blocked original authenticated heartbeat");throwMetadataDump=true;renderer(body);expect(lastStatus==200&&lastWireBody==body&&receivedRevisions.empty(),"optional serialization failure blocked original heartbeat");renderer(body);expect(lastStatus==200&&receivedRevisions==std::vector<unsigned long long>{1},"notification did not retry on the next ordinary heartbeat");expect(metadataParseCalls==3&&metadataDumpCalls==2,"fault injection did not exercise both optional operations");
  }else if(scenario.rfind("during-",0)==0||scenario.rfind("fallback-during-",0)==0){
   const bool fallback=scenario.rfind("fallback-",0)==0;const auto edge=scenario.substr(scenario.find("during-")+7);
   note();const auto body=(fallback?std::string(" "):std::string{})+validBody();metadataDumpHook=[&](){if(edge=="epoch")g_monitorFanDisplay.invalidate();else if(edge=="power")++fanGeneration;else if(edge=="lease")fanLeaseKeepaliveArm("fixture-successor-lease","fixture-replacement");else fanLeaseKeepaliveDisarm("fixture-off");};renderer(body);expect(metadataDumpCalls+canonicalBuildCalls==1,"boundary injection did not reach a payload builder");expect(lastWireBody==body&&receivedRevisions.empty(),"retired metadata crossed a current identity boundary");if(edge=="power")note();if(edge=="off")fanLeaseKeepaliveArm("fixture-next-manual-lease","fixture-manual");renderer(validBody());expect(lastStatus==200,"current manual heartbeat rejected after retired metadata");expect(receivedRevisions==std::vector<unsigned long long>{edge=="power"?2ULL:1ULL},"next current heartbeat omitted the real mode revision");
  }else if(scenario=="canonical-body"){
   note();for(const auto& token:std::vector<std::string>{"Ab_12-cD",std::string(80,'a'),"fixture-lease-0001"}){
    fanLeaseKeepaliveArm(token,"fixture-canonical");const auto body=validBody();renderer(body);
    expect(lastStatus==200,"canonical request rejected");expect(lastWireBody==json{{"leaseId",token},{"msiFirmwareModeRevision",1}}.dump(),"canonical wire bytes changed");
   }
   expect(metadataParseCalls==0&&metadataDumpCalls==0,"standard heartbeat still parses/serializes JSON");expect(canonicalBuildCalls==3,"standard heartbeat did not use bounded direct builder");expect(rendererPosts==3&&nativePosts==0,"metadata optimization changed request count");
  }else if(scenario=="fallback-bodies"){
   note();const std::vector<std::string> bodies={std::string(" \t")+validBody()+"\n",R"({"leaseId":"fixture\u002dlease\u002d0001"})",R"({"tag":7,"leaseId":"fixture-lease-0001"})",R"({"leaseId":"fixture-lease-0001","tag":"quoted\"value"})"};
   for(const auto& body:bodies){auto expected=json::parse(body);expected["msiFirmwareModeRevision"]=1;renderer(body);expect(lastStatus==200,"compatible fallback request rejected");expect(lastWireBody==expected.dump(),"fallback wire fields changed");}
   expect(metadataParseCalls==bodies.size()&&metadataDumpCalls==bodies.size()&&canonicalBuildCalls==0,"fallback stopped using the original parser");
  }else if(scenario=="canonical-invalid"){
   note();const std::vector<std::string> bodies={R"({"leaseId":"short"})",R"({"leaseId":"fixture lease 0001"})",R"({"leaseId":"fixture-lease-0001",})",validBody()+"x",R"({"leaseId":null})"};
   for(const auto& body:bodies){renderer(body);expect(lastWireBody==body,"invalid input was directly spliced");expect(lastStatus!=200,"invalid lease/JSON was accepted");}
   expect(canonicalBuildCalls==0,"invalid canonical-looking input used the direct builder");
  }else if(scenario=="canonical-build-failure"){
   note();const auto body=validBody();throwCanonicalBuild=true;renderer(body);expect(lastStatus==200&&lastWireBody==body&&receivedRevisions.empty(),"optional direct build failure blocked original request");renderer(body);expect(lastStatus==200&&receivedRevisions==std::vector<unsigned long long>{1},"next ordinary heartbeat did not retry metadata");expect(canonicalBuildCalls==2&&metadataParseCalls==0&&metadataDumpCalls==0,"direct builder failure did not exercise its fallback-to-original contract");
  }else if(scenario=="cold-generation"){
   sharedGeneration=fanGeneration=0;note();renderer(validBody());expect(g_fanMsiModeRevision==1&&receivedRevisions==std::vector<unsigned long long>{1},"initial awake generation was wrongly excluded");
  }else throw std::runtime_error("unknown scenario");
  std::cout<<json{{"ok",failures.empty()},{"scenario",scenario},{"checks",checks},{"failures",failures},{"rendererPosts",rendererPosts},{"nativePosts",nativePosts},{"receivedRevisions",receivedRevisions},{"lastStatus",lastStatus},{"metadataParseCalls",metadataParseCalls},{"metadataDumpCalls",metadataDumpCalls},{"canonicalBuildCalls",canonicalBuildCalls},{"wireBodies",wireBodies},{"modelLeaseExpired",modelExpired},{"modelExpiresAt",modelExpiresAt},{"controlReceipts",controlReceipts},{"renewalTimes",renewalTimes},{"hardwareCalls",0},{"cpuBenchmark",false}}.dump()<<'\n';return failures.empty()?0:1;
 }catch(const std::exception& error){std::cout<<json{{"ok",false},{"fixtureError",error.what()},{"scenario",scenario}}.dump()<<'\n';return 2;}
}
