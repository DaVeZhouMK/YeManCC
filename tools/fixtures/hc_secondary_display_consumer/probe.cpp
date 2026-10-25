#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "secondary_display_client.h"
using json=nlohmann::json;
using ULONGLONG=unsigned long long;
struct {bool isolated=false;} g_aiFanMockSession;
enum class PowerLifecycle:uint8_t{Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{1};
static std::atomic<bool> g_hardwareWriteGate{true},g_exitRequested{false},g_shortcutRecordingActive{false};
static std::atomic<unsigned int> g_hardwareWriteInFlight{0};
static std::atomic<uint32_t> g_oemShortcutEdgeEpoch{1};
static std::atomic<uint64_t> g_oemRearMapToL5{0},g_oemRearMapToR5{0};
static constexpr uint64_t kOemBitScreen=1ull<<49,kOemShortcutUnboundEpoch=~uint64_t{0};
struct {bool resolved=true;std::string deviceClass="AYANEOFlipDS";} g_machineIdentity;
struct NativeKeyboardShortcutRule{uint64_t requiredMask=0;};
static std::vector<NativeKeyboardShortcutRule> g_nativeKeyboardShortcuts;
static long long g_nativeKeyboardShortcutRevision=1;
static const std::wstring POWER_CONTROL_DIR=L"fixture-never-opened";
static std::wstring yemancc_data_dir(){return L"fixture-never-opened";}
static void traceLog(const char*,...){}
static std::vector<json> diagnostic;
static void appendNativeLifecycleLog(const char* name,json detail){detail["event"]=name;diagnostic.push_back(std::move(detail));}
enum class SerialJobKind{YmccOwn,ExternalInjection};
struct OemProducerTokens{};
static bool producerCurrent=true,queueAccept=true;
static std::deque<std::function<void()>> jobs;
static bool oemProducerTokensCurrent(const OemProducerTokens&){return producerCurrent;}
static bool msiWmiShortcutAuthoritative(){return false;}
static bool msiWmiGenerationCurrent(uint64_t){return true;}
static bool gamepadSerialSubmit(std::function<void()> job,SerialJobKind kind){if(kind!=SerialJobKind::YmccOwn)throw std::runtime_error("wrong action queue");if(!queueAccept)return false;jobs.push_back(std::move(job));return true;}
static std::map<std::string,std::function<json(const json&)>> apis;
static void ipc_on(const std::string& name,std::function<json(const json&)> handler){if(!apis.emplace(name,std::move(handler)).second)throw std::runtime_error("duplicate API");}
static std::vector<json> requests;
static std::optional<json> nextResponse;
static std::function<void(const std::string&)> afterTransport;
static int raw=200;
static json receipt(const std::string& op){
 const auto id=ymcc::secondarydisplay::modelId(g_machineIdentity.deviceClass);
 json result{{"schema",1},{"ok",true},{"supported",true},{"modelId",id},{"capabilityId",ymcc::secondarydisplay::capabilityId(id)},
  {"raw",raw},{"brightness",(raw*100+127)/255},{"enabled",raw!=0},{"fence",op=="secondaryDisplay.get"?std::string(64,'a'):""},
  {"verification",op=="secondaryDisplay.release"?"released-no-restore":"ec-register-readback"},{"reason",""},{"stage","complete"}};
 return result;
}
namespace ymcc::batterycharge {
static json request(const std::wstring&,const std::wstring&,const json& request){
 requests.push_back(request);auto op=request.at("op").get<std::string>();json result;
 if(nextResponse){result=*nextResponse;nextResponse.reset();}
 else{if(op=="secondaryDisplay.set"){auto target=request["target"].get<int>();raw=(target<0?0:target>100?100:target)*255/100;}
  else if(op=="secondaryDisplay.toggle")raw=raw?0:200;result=receipt(op);}
 if(afterTransport)afterTransport(op);return result;
}
}
@@PRODUCTION@@
static int checks=0,cases=0;
static void check(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
static void test(const char* name,const std::function<void()>& body){body();++cases;std::cerr<<"PASS "<<name<<'\n';}
static void drain(){while(!jobs.empty()){auto job=std::move(jobs.front());jobs.pop_front();job();}}
static void reset(){
 jobs.clear();requests.clear();diagnostic.clear();nextResponse.reset();afterTransport={};g_machineIdentity={true,"AYANEOFlipDS"};
 g_nativeKeyboardShortcuts.clear();g_oemRearMapToL5=0;g_oemRearMapToR5=0;g_shortcutRecordingActive=false;g_exitRequested=false;
 g_hardwareWriteGate=true;g_powerLifecycle=PowerLifecycle::Ready;g_aiFanMockSession.isolated=false;++g_powerGeneration;++g_oemShortcutEdgeEpoch;++g_nativeKeyboardShortcutRevision;producerCurrent=queueAccept=true;raw=200;
}
static void key(uint64_t mask,uint64_t scope=0){nativeSecondaryDisplayKeyboardConsume(mask,g_oemShortcutEdgeEpoch.load(),scope);}
static void press(){key(0);key(kOemBitScreen);}
int main(){try{
 test("registration creates four APIs without helper traffic",[]{reset();reg_secondary_display();check(apis.size()==4&&requests.empty(),"registration performed IO");for(const auto& [name,handler]:apis)check(name.starts_with("model.secondaryDisplay."),"wrong API");});
 test("native get set and release reach exact helper operations",[]{reset();const auto read=apis.at("model.secondaryDisplay.get")(json::object());check(read.value("ok",false),"get failed");
  const auto result=apis.at("model.secondaryDisplay.set")({{"target",50},{"fence",read["fence"]}});check(result.value("ok",false)&&raw==127,"actual set consumer failed");
  check(apis.at("model.secondaryDisplay.release")(json::object()).value("ok",false),"release failed");check(requests.size()==3&&requests[1]["op"]=="secondaryDisplay.set","wrong helper op");check(g_hardwareWriteInFlight==0,"leaked hardware lease");});
 test("native exact aliases admit both frozen models",[]{reset();for(const auto& model:{"AYANEOFlipDS","HandheldCompanion.Devices.AYANEOFlipDS","AYANEOFlip1SDS","HandheldCompanion.Devices.AYANEO.AYANEOFlip1SDS"}){g_machineIdentity.deviceClass=model;check(nativeSecondaryDisplayRequest("get",json::object()).value("ok",false),"exact model rejected");}});
 test("unknown and adjacent models cannot issue helper operations",[]{reset();for(const auto& model:{"AYANEO","AYANEOFlipKB","AYANEOFlip1SKB","xAYANEOFlipDS","AYANEOFlipDS.extra"}){g_machineIdentity.deviceClass=model;check(!nativeSecondaryDisplayRequest("get",json::object()).value("ok",true),"family fallback admitted");}check(requests.empty(),"unknown model dispatched");});
 test("signed short boundary is preserved to HC clamp consumer",[]{reset();for(int target:{-32768,-1,0,1,100,32767}){auto result=nativeSecondaryDisplayRequest("set",{{"target",target},{"fence",std::string(64,'a')}});check(result.value("ok",false),"short target rejected");check(requests.back()["target"]==target,"target changed before provider");}});
 test("malformed requests never launch helper",[]{reset();for(const json& target:{json(-32769),json(32768),json(1.5),json("50"),json(nullptr)})check(!nativeSecondaryDisplayRequest("set",{{"target",target},{"fence",std::string(64,'a')}}).value("ok",true),"invalid target admitted");
  check(!nativeSecondaryDisplayRequest("toggle",{{"fence","invalid"}}).value("ok",true),"invalid fence admitted");check(!nativeSecondaryDisplayRequest("other",json::object()).value("ok",true),"unknown op admitted");check(requests.empty(),"invalid request dispatched");});
 test("closed power and unresolved identity prevent IO",[]{reset();g_hardwareWriteGate=false;check(!nativeSecondaryDisplayRequest("get",json::object()).value("ok",true),"closed power admitted");g_hardwareWriteGate=true;g_machineIdentity.resolved=false;check(!nativeSecondaryDisplayRequest("get",json::object()).value("ok",true),"unresolved identity admitted");check(requests.empty()&&g_hardwareWriteInFlight==0,"power block performed IO");});
 test("provider failure retains original raw and stage evidence",[]{reset();nextResponse={{"ok",false},{"reason","write acknowledgement lost"},{"raw",127},{"stage","preference-save"},{"fence","secret"}};
  auto result=nativeSecondaryDisplayRequest("set",{{"target",50},{"fence",std::string(64,'a')}});check(!result.value("ok",true)&&result["providerResult"]["raw"]==127,"failure evidence discarded");check(!result["providerResult"].contains("fence"),"logged live authority");check(requests.size()==1,"uncertain write retried");});
 test("late power change rejects success while retaining provider result",[]{reset();afterTransport=[](const std::string&){++g_powerGeneration;};auto result=nativeSecondaryDisplayRequest("get",json::object());check(!result.value("ok",true)&&result.contains("providerResult"),"late power result accepted");check(g_hardwareWriteInFlight==0,"late result leaked lease");});
 test("wrong source and malformed readbacks never establish success",[]{reset();for(const auto& field:{"modelId","capabilityId","verification","raw","brightness","enabled","fence"}){
  auto altered=receipt("secondaryDisplay.get");std::string name(field);if(name=="raw")altered[field]=256;else if(name=="brightness")altered[field]=999;else if(name=="enabled")altered[field]=false;else altered[field]="wrong";
  nextResponse=altered;check(!nativeSecondaryDisplayRequest("get",json::object()).value("ok",true),"malformed readback accepted");}
  for(const auto* field:{"schema","brightness"}){auto huge=receipt("secondaryDisplay.get");huge[field]=huge[field].get<std::int64_t>()+(std::int64_t{1}<<32);nextResponse=huge;check(!nativeSecondaryDisplayRequest("get",json::object()).value("ok",true),"oversized integer aliased into receipt");}
  nextResponse=json::array();check(!nativeSecondaryDisplayRequest("get",json::object()).value("ok",true),"array receipt accepted");});
 test("set result must match exact target and complete stage",[]{reset();nextResponse=receipt("secondaryDisplay.set");check(!nativeSecondaryDisplayRequest("set",{{"target",50},{"fence",std::string(64,'a')}}).value("ok",true),"wrong target readback accepted");auto altered=receipt("secondaryDisplay.toggle");altered["stage"]="latch-write";nextResponse=altered;check(!nativeSecondaryDisplayRequest("toggle",{{"fence",std::string(64,'a')}}).value("ok",true),"partial write accepted");});
 test("dedicated screen key requires neutral then one rising edge",[]{reset();key(kOemBitScreen);check(jobs.empty(),"held startup generated action");press();key(kOemBitScreen);key(kOemBitScreen|(1ull<<20));check(jobs.size()==1,"held key repeated");drain();check(requests.size()==2&&requests[0]["op"]=="secondaryDisplay.get"&&requests[1]["op"]=="secondaryDisplay.toggle"&&raw==0,"actual key did not consume get-toggle chain");});
 test("existing mapping and rear mapping suppress default",[]{reset();g_nativeKeyboardShortcuts.push_back({kOemBitScreen});press();check(jobs.empty(),"mapped shortcut triggered default");g_nativeKeyboardShortcuts.clear();key(kOemBitScreen);check(jobs.empty(),"mapping removal inherited held key");press();check(jobs.size()==1,"unmapped neutral press not consumed");jobs.clear();g_oemRearMapToL5=kOemBitScreen;press();check(jobs.empty(),"rear mapping triggered default");});
 test("recording and other producers cannot trigger default",[]{reset();g_shortcutRecordingActive=true;press();check(jobs.empty(),"recording toggled display");g_shortcutRecordingActive=false;key(kOemBitScreen);check(jobs.empty(),"recording end inherited held key");key(0);key(kOemBitScreen,1ull<<20);check(jobs.empty(),"other producer triggered screen");});
 test("queued source rule and parent lifetime are checked",[]{for(int mode=0;mode<5;mode++){reset();press();check(jobs.size()==1,"missing queued action");if(mode==0)++g_oemShortcutEdgeEpoch;else if(mode==1)g_shortcutRecordingActive=true;else if(mode==2)g_exitRequested=true;else if(mode==3)++g_powerGeneration;else producerCurrent=false;drain();check(requests.empty(),"stale queued action reached helper");}});
 test("get cannot authorize toggle after mapping recording source or power change",[]{for(int mode=0;mode<5;mode++){reset();press();afterTransport=[mode](const std::string& op){if(op!="secondaryDisplay.get")return;if(mode==0)++g_oemShortcutEdgeEpoch;else if(mode==1)g_shortcutRecordingActive=true;else if(mode==2)g_oemRearMapToL5=kOemBitScreen;else if(mode==3)++g_powerGeneration;else ++g_nativeKeyboardShortcutRevision;};drain();check(requests.size()==1,"stale read authorized toggle");}});
 test("model and power changes require a new neutral sample",[]{reset();press();drain();requests.clear();g_machineIdentity.deviceClass="AYANEOFlip1SDS";key(kOemBitScreen);check(jobs.empty(),"new model inherited held key");press();check(jobs.size()==1,"new model neutral press lost");jobs.clear();++g_powerGeneration;key(kOemBitScreen);check(jobs.empty(),"resume inherited held key");});
 test("failed get never becomes toggle or automatic held retry",[]{reset();press();nextResponse={{"ok",false},{"reason","controlled failure"}};drain();check(requests.size()==1,"failed get toggled");key(kOemBitScreen);check(jobs.empty(),"held key retried failure");});
 test("inactive high frequency samples add zero helper operations",[]{reset();for(int i=0;i<10000;i++)key(0);check(jobs.empty()&&requests.empty(),"idle polling introduced IO");});
 std::cout<<json({{"schemaVersion",1},{"status","PASS"},{"cases",cases},{"checks",checks},{"scope","extracted actual native request, queue wrapper, OEM consumer and registration with controlled helper transport"},{"physicalAcceptance",false},{"wholeProductExecuted",false}}).dump()<<'\n';return 0;
}catch(const std::exception& e){std::cout<<json({{"status","FAIL"},{"cases",cases},{"checks",checks},{"error",e.what()}}).dump()<<'\n';return 1;}}