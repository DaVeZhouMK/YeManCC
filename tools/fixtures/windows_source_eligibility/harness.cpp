// Production qualification/IPC/export; controlled cache, clock and recipe producers.
#include "windows_source_eligibility.h"
#include "json.hpp"
#include <atomic>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
using json=nlohmann::json;using DWORD=std::uint32_t;
namespace inv=ymcc::sensors::inventory;
struct Sink{std::atomic<std::uint64_t> activeBindingGeneration{20};};
struct InputCaptureSensorMeta{std::string gyroDeviceId,accelDeviceId,gyroFamily,accelFamily;bool gyroPresent=false,accelPresent=false;};
struct Machine{std::string deviceClass;};static Machine g_machineIdentity;
static std::string activeMotionClass;
static const std::string& inputCaptureMotionDeviceClass(){return activeMotionClass.empty()?g_machineIdentity.deviceClass:activeMotionClass;}
static std::shared_ptr<Sink> sink;
static InputCaptureSensorMeta meta;
static std::uint64_t clockNow=1000,power=3;
static json cachedProbe,recipe,capability;
static std::function<void()> onProbe;
static unsigned probeReads=0,wrongRecipeFamily=0,fileWrites=0;
static std::map<std::wstring,std::string> files;
static bool writeOk=true;static DWORD lastError=41;
static auto GetTickCount64(){return clockNow;}
static json inputCaptureWindowsSourceSelectionStatus(){return {{"queryDoesNotSample",true},{"hostSubmissionProven",false},{"gameConsumptionProven",false}};}
static InputCaptureSensorMeta inputCaptureSensorMetaSnapshot(){return meta;}
static inv::Ticket inputCaptureWindowsProbeCurrentTicket(){return{ymcc::windows_motion::retain(sink,sink->activeBindingGeneration.load()),power};}
static json inputCaptureCandidateMatrixReceipt(const InputCaptureSensorMeta& value){if(value.gyroFamily!="winrt"||value.accelFamily!="winrt")++wrongRecipeFamily;return recipe;}
static json inputCaptureMotionCapabilityReceipt(const InputCaptureSensorMeta&,bool){return capability;}
static json inputCaptureWindowsSourceProbeStatus(){++probeReads;auto result=cachedProbe;if(onProbe)onProbe();return result;}
static json inputCaptureWindowsSensorStatus(){return{{"fixtureCacheBoundary",true}};}
static DWORD GetLastError(){return lastError;}static void SetLastError(DWORD value){lastError=value;}
static std::wstring yemancc_data_dir(){return L"fixture";}static std::string nativeLifecycleTimestamp(){return "fixture-time";}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& value){++fileWrites;lastError=99;if(!writeOk)return false;files[path]=value;return true;}
// @@PROVIDER@@
// @@SELECTED_META@@
// @@AXES@@
// @@EVIDENCE@@
// @@QUERY@@
// Processor cache is controlled in this query-only fixture; its real worker has an independent DLL suite.
static json inputCaptureWindowsProcessingReceiptSnapshot(){return {{"fixtureProcessorCache",true}};}
// @@EXPORT@@
static std::map<std::string,std::function<json(const json&)>> handlers;
static void ipc_on(const std::string& op,std::function<json(const json&)> handler){handlers[op]=std::move(handler);}
static void registerQuery(){
// @@IPC@@
}
static unsigned checks=0;static json failures=json::array();static void check(bool ok,const std::string& text){++checks;if(!ok)failures.push_back(text);}
static const std::string gid="ACPI#IMUA#1#{GYRO}",aid="ACPI#IMUA#1#{ACCEL}";
static json axes(std::uint64_t time,std::uint64_t receipt){return{{"x",1.25},{"y",2.5},{"z",-3.75},{"timestamp100ns",time},{"receiptUptimeMs",receipt}};}
static json part(const std::string& kind,const std::string& id){return{{"kind",kind},{"deviceId",id},{"requested",true},{"opened",true},{"openHresult",std::int64_t{0}},{"distinctTimestamps",std::uint64_t{2}},{"lastReportUsable",true},{"readingTransform",0},{"readingTransformHresult",std::int64_t{0}},{"unit",kind=="gyro"?"degrees-per-second":"g"},{"first",axes(100,998)},{"last",axes(101,1000)}};}
static void reset(){sink=std::make_shared<Sink>();meta={"selected-gyro","selected-accel","sensorapi","sensorapi",true,true};power=3;clockNow=1000;g_machineIdentity.deviceClass="fixture-model";activeMotionClass.clear();onProbe={};probeReads=wrongRecipeFamily=fileWrites=0;writeOk=true;lastError=41;files.clear();
 recipe={{"gyro",{{"id","gyro:z-x-y"},{"sourceSha256",std::string(64,'a')}}},{"accel",{{"id","accel:y-z-x"},{"sourceSha256",std::string(64,'b')}}}};
 capability={{"deviceClass",g_machineIdentity.deviceClass},{"sourceRoute","windows-sensors"},{"modelSelectorBound",true}};
 cachedProbe={{"recorded",true},{"contextCurrent",true},{"observationSerial",std::uint64_t{7}}};
 cachedProbe["lastObservation"]={{"deviceClass",g_machineIdentity.deviceClass},{"pairs",json::array()}};
 cachedProbe["lastObservation"]["pairs"].push_back({{"parts",json::array({part("gyro",gid),part("accel",aid)})}});
}
static json& raw(unsigned kind){return cachedProbe["lastObservation"]["pairs"][0]["parts"][kind];}
static json query(){return inputCaptureWindowsSourceEligibilityStatus();}
static json first(){const auto value=query();return value["pairs"].empty()?json::object():value["pairs"][0];}
int main(int argc,char**argv){try{
 reset();cachedProbe={{"recorded",false},{"contextCurrent",false}};auto value=query();check(value["sourceCount"].is_null()&&value["pairCandidateCount"].is_null(),"query/unobserved counts stay unknown");check(!value["recorded"].get<bool>()&&value["pairs"].empty(),"query/unobserved has no candidates");
 reset();cachedProbe["lastObservation"]["pairs"]=json::array();value=query();check(value["sourceCount"]==0&&value["pairCandidateCount"]==0,"query/observed empty differs from unknown");
 reset();const auto cacheBefore=cachedProbe,metaBefore=inputCaptureWindowsProbeSelectedMeta(),recipeBefore=recipe;value=query();auto option=value["pairs"][0];check(value["contextCurrent"]==true&&value["sourceCount"]==2&&value["pairCandidateCount"]==1,"query/complete pair and current context");check(option["canPrepareExplicitModelConfiguration"]==true&&option["freshCompleteReportedData"]==true&&option["nativeReadingFrame"]==true,"query/fresh native-frame evidence may stage explicit configuration");check(value["selectionBackendIntegrated"]==bool(YMCC_FIXTURE_SELECTION_BACKEND_INTEGRATED)&&value["automaticSelectionAllowed"]==false&&value["physicalAxesVerified"]==false,"query/backend availability does not claim automatic selection or physical acceptance");check(value["requiredTransformPolicy"]=="explicit-current-model","query/explicit transform policy required");check(value["queryOpensSensors"]==false&&value["selectionWritten"]==false&&value["calibrationWritten"]==false&&value["samplesPublishedToInput"]==false,"query/value-only diagnostic flags");check(cacheBefore==cachedProbe&&metaBefore==inputCaptureWindowsProbeSelectedMeta()&&recipeBefore==recipe&&fileWrites==0,"query/cache selection recipe remain unchanged");check(wrongRecipeFamily==0,"query/prospective WinRT recipe does not borrow current SensorAPI override");
 reset();clockNow=3000;option=first();check(option["completeReportedObservation"]==true&&option["canPrepareExplicitModelConfiguration"]==true&&option["freshCompleteReportedData"]==false,"query/old observation stages new reads but is not current input");
 reset();clockNow=999;check(first()["canPrepareExplicitModelConfiguration"]==false,"query/future receipt rejected");
 for(unsigned kind=0;kind<2;++kind){
  reset();raw(kind)["readingTransform"]=2;check(first()["canPrepareExplicitModelConfiguration"]==false&&first()["nativeReadingFrame"]==false,"query/remapped reading frame rejected "+std::to_string(kind));
  reset();raw(kind).erase("readingTransform");check(first()["reason"]=="reading-frame-unobserved","query/unobserved reading frame "+std::to_string(kind));
  reset();raw(kind)["readingTransformHresult"]=-88;check(first()["canPrepareExplicitModelConfiguration"]==false,"query/reading frame getter error "+std::to_string(kind));
  reset();raw(kind)["lastReportUsable"]=false;check(first()["completeReportedObservation"]==false,"query/latest invalid report revokes usability "+std::to_string(kind));
  reset();raw(kind)["opened"]=false;check(first()["canPrepareExplicitModelConfiguration"]==false,"query/unopened source "+std::to_string(kind));
  reset();raw(kind)["openHresult"]=-1;check(first()["canPrepareExplicitModelConfiguration"]==false,"query/open failure "+std::to_string(kind));
  reset();raw(kind)["unit"]="unscaled";check(first()["canPrepareExplicitModelConfiguration"]==false,"query/unknown unit "+std::to_string(kind));
  reset();raw(kind)["last"]["x"]=nullptr;check(first()["canPrepareExplicitModelConfiguration"]==false,"query/missing numeric component "+std::to_string(kind));
  reset();raw(kind)["last"]["x"]=1e100;check(first()["canPrepareExplicitModelConfiguration"]==false,"query/out-of-range numeric component "+std::to_string(kind));
  reset();raw(kind)["last"]["timestamp100ns"]=std::uint64_t{100};check(first()["canPrepareExplicitModelConfiguration"]==false,"query/cached timestamp "+std::to_string(kind));
  reset();raw(kind)["last"]["timestamp100ns"]=-1;check(first()["canPrepareExplicitModelConfiguration"]==false,"query/negative timestamp "+std::to_string(kind));
 }
 for(const std::string field:{"x","y","z","timestamp100ns","receiptUptimeMs"}){reset();raw(0)["first"].erase(field);check(first()["canPrepareExplicitModelConfiguration"]==false,"query/missing first field "+field);}
 reset();cachedProbe["contextCurrent"]=false;check(first()["canPrepareExplicitModelConfiguration"]==false,"query/retired probe context rejected");
 reset();cachedProbe["lastObservation"]["deviceClass"]="other-model";check(first()["canPrepareExplicitModelConfiguration"]==false,"query/probe from different model rejected");
 reset();capability["modelSelectorBound"]=false;check(first()["reviewedModelRecipeAvailable"]==false&&first()["canPrepareExplicitModelConfiguration"]==false,"query/unbound model rejected");
 reset();capability["sourceRoute"]="controller-specific";check(first()["canPrepareExplicitModelConfiguration"]==false,"query/controller route cannot borrow Windows policy");
 reset();recipe["gyro"]["sourceSha256"]="bad";check(first()["canPrepareExplicitModelConfiguration"]==false,"query/recipe source digest must be full");
 reset();recipe["gyro"]["id"]="";check(first()["canPrepareExplicitModelConfiguration"]==false,"query/recipe matrix identity required");
 reset();raw(1)["deviceId"]="ACPI#OTHER#1#{ACCEL}";check(query()["pairs"].empty(),"query/different providers not associated");
 reset();auto extra=raw(0);extra["deviceId"]="acpi#imua#1#{gyro}";cachedProbe["lastObservation"]["pairs"][0]["parts"].push_back(extra);option=first();check(option["reason"]=="duplicate-source-evidence"&&option["canPrepareExplicitModelConfiguration"]==false,"query/duplicate case-folded source denied");
 reset();extra=raw(0);extra["deviceId"]="ACPI#IMUA#1#{GYRO-B}";cachedProbe["lastObservation"]["pairs"][0]["parts"].push_back(extra);value=query();check(value["sourceCount"]==3&&value["pairCandidateCount"]==2,"query/multiple gyros are explicit alternatives");for(const auto& row:value["pairs"])check(row["automaticSelectionAllowed"]==false&&row["physicalAxesVerified"]==false,"query/multiple gyros never auto-paired or physically verified");
 for(const std::string mutation:{"owner","epoch","power","metadata","recipe","class","binding","clock"}){reset();onProbe=[mutation]{if(mutation=="owner")sink=std::make_shared<Sink>();if(mutation=="epoch")++sink->activeBindingGeneration;if(mutation=="power")++power;if(mutation=="metadata")meta.gyroDeviceId="changed";if(mutation=="recipe")recipe["gyro"]["id"]="changed";if(mutation=="class")g_machineIdentity.deviceClass="changed";if(mutation=="binding")capability["modelSelectorBound"]=false;if(mutation=="clock")clockNow=999;};value=query();check(value["contextCurrent"]==false&&value["pairs"][0]["canPrepareExplicitModelConfiguration"]==false,"query/context changes denied: "+mutation);}
 for(const json malformed:{json(nullptr),json::array(),json{{"recorded","bad-type"}},json{{"recorded",true},{"contextCurrent",true},{"observationSerial",std::uint64_t{7}},{"lastObservation",{{"deviceClass","fixture-model"},{"pairs",json::array({{{"parts",json::array({{{"requested",true},{"kind",7}}})}}})}}}}}){reset();cachedProbe=malformed;value=query();check(value.value("reason",std::string{})=="qualification-query-failed"&&value["recorded"]==false&&value["selectionBackendIntegrated"]==bool(YMCC_FIXTURE_SELECTION_BACKEND_INTEGRATED),"query/malformed cache fails closed");}
 for(const std::string fallback:{"HandheldCompanion.Devices.DefaultDevice","HandheldCompanion.Devices.ASUS.ASUS"}){
  reset();g_machineIdentity.deviceClass.clear();activeMotionClass=fallback;capability["deviceClass"]=fallback;capability["machineDeviceClass"]="";capability["genericFallback"]=true;cachedProbe["lastObservation"]["deviceClass"]="";
  value=query();check(value["contextCurrent"]==true&&value["deviceClass"]==fallback&&value["pairs"][0]["canPrepareExplicitModelConfiguration"]==true,"query/generic recipe identity remains separate from raw machine identity");
  cachedProbe["lastObservation"]["deviceClass"]="other-machine";value=query();check(value["contextCurrent"]==false&&value["pairs"][0]["canPrepareExplicitModelConfiguration"]==false,"query/generic cannot borrow another machine observation");
 }
 reset();registerQuery();check(handlers.count("gyro.windowsSourceEligibility.get")==1,"query/actual IPC registered");const auto callsBefore=probeReads;value=handlers.at("gyro.windowsSourceEligibility.get")(json::object());check(probeReads==callsBefore+1&&value["pairCandidateCount"]==1,"query/actual IPC executes qualification");
 reset();const auto exported=exportWindowsSensorEvidence();check(!exported.empty()&&fileWrites==1&&lastError==41,"query/actual export saves and preserves caller error");const auto saved=json::parse(files.at(exported));check(saved["sourceEligibility"]["pairs"][0]["canPrepareExplicitModelConfiguration"]==true&&saved["sourceProbe"]==cachedProbe&&saved["exportEntry"]=="logs.exportAll","query/actual export includes current qualification and raw evidence");writeOk=false;check(exportWindowsSensorEvidence().empty()&&lastError==41,"query/export failure cannot advertise stale evidence");
 }catch(const std::exception& e){failures.push_back(std::string("fixture exception: ")+e.what());}
 json report={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"cacheAndRecipeProducersControlled",true},{"productionQueryIpcExportExecuted",true},{"physicalAcceptance",false}};if(argc>1)std::ofstream(argv[1])<<report.dump(2);std::cout<<report.dump()<<std::endl;return failures.empty()?0:1;}
