#include "sensor_device_evidence.h"
#include "sensor_device_change.h"
#include <dbt.h>
#include <devpkey.h>
#include <sstream>
namespace ci=ymcc::sensors::sensorapi_inventory;
namespace inv=ymcc::sensors::inventory;
using json=nlohmann::json;
static unsigned checks=0;static std::vector<std::string> failures;
static void expect(bool ok,const std::string& text){++checks;if(!ok)failures.push_back(text);}
struct Sink {std::atomic<std::uint64_t> activeBindingGeneration{73};};
static std::shared_ptr<Sink> g_inputCaptureSink=std::make_shared<Sink>();
static std::uint64_t power=7;static unsigned ticketCalls=0;static std::function<void()> ticketHook;
static std::uint64_t currentPowerGeneration(){if(ticketHook)ticketHook();return power;}
enum class YmccFamily { Unknown, OneXPlayer, MsiClaw, Ayanero, Lenovo, Asus, Gpd };
// Keep the original metadata-query defaults. Recovery cases opt into a resolved OneX.
struct Machine {std::string deviceClass="fixture-original-model";bool resolved=false;YmccFamily family=YmccFamily::Unknown;};
using MachineIdentity=Machine;
static Machine g_machineIdentity;
static std::string W2U(const std::wstring& value){if(value.empty())return {};const auto size=WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);std::string result(size,0);WideCharToMultiByte(CP_UTF8,0,value.data(),static_cast<int>(value.size()),result.data(),size,nullptr,nullptr);return result;}
namespace de=ymcc::sensors::device_evidence;
// Transport fakes only: the collector, decoder, bounds and JSON are production templates.
struct DeviceEvidenceOps {
 std::uint64_t now=0;unsigned apiCalls=0,enumCalls=0,liveSets=0,liveServices=0,closedSets=0,closedServices=0,lateCalls=0;
 unsigned count=1,parents=2;int failEnumAt=-1;bool denySet=false,denyScm=false,denyProperty=false,failNodeStatus=false,failParent=false;
 bool wrongType=false,badBool=false,badString=false,hugeProperty=false,manyIds=false,bulky=false,throwProperty=false;
 bool missingService=false,denyConfig=false,failServiceStatus=false,disabledService=false,failIdentity=false;
 std::string slowApi;std::uint64_t slowMs=1501,closeDelay=0;std::uintptr_t nextHandle=20;
 std::map<SC_HANDLE,std::wstring> serviceNames;
 std::uint64_t monotonicMs()const{return now;}
 void touch(const char* api){if(now>=de::budget_ms)++lateCalls;++apiCalls;SetLastError(401);if(slowApi==api)now+=slowMs;}
 static de::Property raw(DEVPROPTYPE type,const void* data,std::size_t size){de::Property p;p.ok=true;p.type=type;p.requiredBytes=static_cast<DWORD>(size);p.bytes.resize(size);if(size)std::memcpy(p.bytes.data(),data,size);return p;}
 template<class T>static de::Property scalar(DEVPROPTYPE type,const T& value){return raw(type,&value,sizeof(value));}
 static de::Property text(const std::wstring& value){return raw(DEVPROP_TYPE_STRING,value.c_str(),(value.size()+1)*sizeof(wchar_t));}
 static de::Property list(unsigned count){std::wstring value;for(unsigned i=0;i<count;++i){value+=L"ACPI\\BMI160_"+std::to_wstring(i);value.push_back(L'\0');}value.push_back(L'\0');if(!count)value.push_back(L'\0');return raw(DEVPROP_TYPE_STRING_LIST,value.data(),value.size()*sizeof(wchar_t));}
 de::Observation observedTime(){touch("time");de::Observation r;r.ok=true;r.fileTimeUtc=134380976317120134ULL;return r;}
 de::Identity identity(){touch("identity");de::Identity r;r.pid=77;r.ok=!failIdentity;r.win32Error=failIdentity?ERROR_ACCESS_DENIED:0;r.creationFileTime=134380976317120134ULL;return r;}
 de::SetResult openSensorClass(){touch("open-class");de::SetResult r;r.ok=!denySet;r.win32Error=denySet?ERROR_ACCESS_DENIED:0;if(r.ok){r.handle=reinterpret_cast<HDEVINFO>(1);++liveSets;}return r;}
 de::NodeResult enumNode(HDEVINFO set,unsigned i){touch("enum");++enumCalls;de::NodeResult r;if(int(i)==failEnumAt){r.win32Error=ERROR_ACCESS_DENIED;return r;}if(i>=count){r.win32Error=ERROR_NO_MORE_ITEMS;return r;}r.ok=true;r.node.devinst=i+1;r.node.set=set;return r;}
 de::Property property(const de::Node& node,const de::PropertySpec& spec){
  touch("property");if(throwProperty)throw std::runtime_error("device-fixture-property-failure");
  if(denyProperty&&spec.field==de::Field::Manufacturer){de::Property p;p.win32Error=ERROR_ACCESS_DENIED;if(node.set==INVALID_HANDLE_VALUE){p.hasConfigRet=true;p.configRet=CR_ACCESS_DENIED;}return p;}
  if(hugeProperty&&spec.field==de::Field::HardwareIds){de::Property p;p.win32Error=ERROR_INSUFFICIENT_BUFFER;p.requiredBytes=de::max_property_bytes+1;return p;}
  if(spec.field==de::Field::ProblemStatus){const std::uint32_t bits=0xc0000001u;return scalar(wrongType?DEVPROP_TYPE_UINT32:DEVPROP_TYPE_NTSTATUS,bits);}
  if(spec.field==de::Field::ProblemCode)return scalar(DEVPROP_TYPE_UINT32,std::uint32_t(0));
  if(spec.field==de::Field::IsPresent){const DEVPROP_BOOLEAN b=badBool?DEVPROP_BOOLEAN(1):(node.set==INVALID_HANDLE_VALUE?DEVPROP_TRUE:DEVPROP_FALSE);return scalar(DEVPROP_TYPE_BOOLEAN,b);}
  if(spec.field==de::Field::ClassGuid)return scalar(DEVPROP_TYPE_GUID,de::sensor_class);
  if(spec.field==de::Field::HardwareIds||spec.field==de::Field::CompatibleIds)return list(manyIds?de::max_array_entries+1:2);
  if(spec.field==de::Field::InstanceId)return text(node.set==INVALID_HANDLE_VALUE?L"ACPI\\AMDI0010\\"+std::to_wstring(node.devinst):L"ACPI\\BMI160\\"+std::to_wstring(node.devinst));
  if(spec.field==de::Field::Service)return text(node.set==INVALID_HANDLE_VALUE?L"amdi2c":L"BoschSensor");
  if(spec.field==de::Field::EnumeratorName)return text(L"ACPI");
  if(spec.field==de::Field::DriverInfPath)return text(L"oem22.inf");
  if(spec.field==de::Field::DriverProvider)return text(bulky?std::wstring(700,L'P'):L"Bosch");
  if(spec.field==de::Field::DriverVersion)return text(bulky?std::wstring(700,L'V'):L"1.0.23.0");
  if(spec.field==de::Field::Description){if(badString){const wchar_t bad[]={L'N',L'O'};return raw(DEVPROP_TYPE_STRING,bad,sizeof(bad));}return text(bulky?std::wstring(700,L'D'):L"Bosch BMI160 Sensor");}
  return text(bulky?std::wstring(700,L'M'):L"Bosch");
 }
 de::NodeStatus nodeStatus(const de::Node&){touch("status");de::NodeStatus r;static_cast<de::Result&>(r)=de::cm_result(failNodeStatus?CR_ACCESS_DENIED:CR_SUCCESS);r.flags=0x1234;r.problem=failNodeStatus?7:0;return r;}
 de::NodeResult parent(const de::Node& node){touch("parent");de::NodeResult r;r.hasConfigRet=true;const unsigned depth=node.devinst>=1000?(node.devinst-1000)%10:0;
  if(failParent){r.configRet=CR_ACCESS_DENIED;return r;}if(depth>=parents){r.configRet=CR_NO_SUCH_DEVNODE;return r;}
  r.ok=true;r.node.devinst=depth?node.devinst+1:1000+node.devinst*10+1;return r;
 }
 de::ServiceHandleResult openScm(){touch("scm");de::ServiceHandleResult r;r.ok=!denyScm;r.win32Error=denyScm?ERROR_ACCESS_DENIED:0;if(r.ok){r.handle=reinterpret_cast<SC_HANDLE>(++nextHandle);++liveServices;}return r;}
 de::ServiceHandleResult openService(SC_HANDLE,const std::wstring& name,DWORD access){touch("open-service");de::ServiceHandleResult r;
  if(missingService){r.win32Error=ERROR_SERVICE_DOES_NOT_EXIST;return r;}if(denyConfig&&access==SERVICE_QUERY_CONFIG){r.win32Error=ERROR_ACCESS_DENIED;return r;}
  r.ok=true;r.handle=reinterpret_cast<SC_HANDLE>(++nextHandle);serviceNames[r.handle]=name;++liveServices;return r;
 }
 de::ServiceStatus serviceStatus(SC_HANDLE){touch("service-status");de::ServiceStatus r;r.ok=!failServiceStatus;r.win32Error=failServiceStatus?ERROR_ACCESS_DENIED:0;r.value.dwCurrentState=SERVICE_RUNNING;r.value.dwProcessId=42;return r;}
 de::ServiceConfig serviceConfig(SC_HANDLE){touch("service-config");de::ServiceConfig r;r.ok=true;r.startType=disabledService?SERVICE_DISABLED:SERVICE_DEMAND_START;return r;}
 void closeSet(HDEVINFO){--liveSets;++closedSets;now+=closeDelay;SetLastError(402);}
 void closeService(SC_HANDLE h){--liveServices;++closedServices;serviceNames.erase(h);SetLastError(403);}
};
static DeviceEvidenceOps deviceEvidenceOps;
static bool deviceSnapshotThrows=false;
static json fixtureDeviceEvidenceSnapshot(){if(deviceSnapshotThrows)throw std::runtime_error("device-export-failure");return de::snapshot_with_ops<json>(deviceEvidenceOps);}
static json deviceSnapshot(DeviceEvidenceOps& ops,const std::string& name){SetLastError(981);auto j=de::snapshot_with_ops<json>(ops);
 expect(GetLastError()==981,name+": snapshot preserves caller last error across API and cleanup");
 expect(ops.liveSets==0&&ops.liveServices==0,name+": owned SetupAPI and SCM/service handles actually drain");
 expect(j["readOnly"]==true&&j["noWrites"]==true&&j["writes"]==false,name+": read-only snapshot contract");
 expect(j["hardwareAbsenceProven"]==false&&j["scopeSufficientForHardwareAbsence"]==false,name+": narrow enumeration never proves hardware absence");
 expect(j.dump(2).size()<=de::max_snapshot_bytes&&j["serializedBytes"]==j.dump(2).size(),name+": actual exported JSON fits the total byte cap");
 expect(ops.lateCalls==0,name+": no further metadata API starts at or beyond the elapsed budget");return j;}
static void deviceEvidenceCases(){
 // Independent SDK oracle: not derived from the production key table or fake return values.
 const DEVPROPKEY* sdkKeys[]={&DEVPKEY_Device_InstanceId,&DEVPKEY_Device_HardwareIds,&DEVPKEY_Device_CompatibleIds,
  &DEVPKEY_Device_ClassGuid,&DEVPKEY_Device_DeviceDesc,&DEVPKEY_Device_Manufacturer,&DEVPKEY_Device_IsPresent,
  &DEVPKEY_Device_ProblemCode,&DEVPKEY_Device_ProblemStatus,&DEVPKEY_Device_DriverInfPath,&DEVPKEY_Device_DriverProvider,
  &DEVPKEY_Device_DriverVersion,&DEVPKEY_Device_Service,&DEVPKEY_Device_EnumeratorName};
 const DEVPROPTYPE sdkTypes[]={DEVPROP_TYPE_STRING,DEVPROP_TYPE_STRING_LIST,DEVPROP_TYPE_STRING_LIST,DEVPROP_TYPE_GUID,
  DEVPROP_TYPE_STRING,DEVPROP_TYPE_STRING,DEVPROP_TYPE_BOOLEAN,DEVPROP_TYPE_UINT32,DEVPROP_TYPE_NTSTATUS,
  DEVPROP_TYPE_STRING,DEVPROP_TYPE_STRING,DEVPROP_TYPE_STRING,DEVPROP_TYPE_STRING,DEVPROP_TYPE_STRING};
 expect(sizeof(de::properties)/sizeof(de::properties[0])==sizeof(sdkKeys)/sizeof(sdkKeys[0]),"device key table covers each independently enumerated SDK key");
 for(unsigned i=0;i<sizeof(sdkKeys)/sizeof(sdkKeys[0]);++i)expect(IsEqualGUID(de::properties[i].key.fmtid,sdkKeys[i]->fmtid)&&de::properties[i].key.pid==sdkKeys[i]->pid&&de::properties[i].type==sdkTypes[i],"device property key and type match SDK entry "+std::to_string(i));
 {DeviceEvidenceOps ops;const auto j=deviceSnapshot(ops,"device success");const auto& row=j["sensorClass"]["nodes"][0];
  expect(j["sensorClass"]["enumerationComplete"]==true&&j["sensorClass"]["includesPhantom"]==true,"sensor class successful enumeration includes phantom scope");
  expect(row["properties"]["isPresent"]["value"]==false&&row["metadataComplete"]==true,"phantom presence is explicit false, never inferred from an enumerated devnode");
  expect(row["properties"]["deviceInstanceId"]["value"]=="ACPI\\BMI160\\1"&&row["properties"]["driverInfPath"]["value"]=="oem22.inf"&&row["properties"]["driverProvider"]["value"]=="Bosch"&&row["properties"]["driverVersion"]["value"]=="1.0.23.0","bound node identity and driver facts come from actual property calls");
  expect(row["properties"]["problemStatus"]["value"]==0xc0000001u&&row["properties"]["problemCode"]["value"]==0,"NTSTATUS bits remain distinct from the actual uint32 problem code");
  expect(row["statusQuery"]["configRet"]==CR_SUCCESS&&row["statusQuery"]["flags"]==0x1234,"CM status preserves raw CONFIGRET and flags");
  expect(row["parents"].size()==2&&row["parents"][0]["properties"]["deviceInstanceId"]["value"].get<std::string>().find("ACPI\\AMDI0010")!=std::string::npos,"actual parent chain retains ACPI/I2C identity and bound properties");
  expect(j["services"]["requestedCount"]==5&&j["services"]["entries"].size()==5,"default service names and unique root/parent bound services are queried");
  expect(j["services"]["entries"][0]["statusQuery"]["currentState"]==SERVICE_RUNNING&&j["services"]["entries"][0]["startTypeQuery"]["startType"]==SERVICE_DEMAND_START,"state and readonly start type are independent observed facts");
  expect(j["processIdentity"]["processId"]==77&&j["processIdentity"]["creationFileTime"]=="134380976317120134"&&j["observedTime"]["fileTimeUtc"]=="134380976317120134","timestamp and process birth are lossless identity facts");
  expect(j["sensorInterfaces"]["status"]=="NOT_COLLECTED"&&j["status"]=="partial","optional interface coverage is explicitly uncollected");
 }
 {DeviceEvidenceOps ops;ops.count=0;const auto j=deviceSnapshot(ops,"device empty");expect(j["sensorClass"]["nodes"].empty()&&j["sensorClass"]["enumerationComplete"]==true&&j["sensorClass"]["enumerationEnd"]["win32Error"]==ERROR_NO_MORE_ITEMS,"observed empty class enumeration retains its successful end marker");}
 {DeviceEvidenceOps ops;ops.denySet=true;const auto j=deviceSnapshot(ops,"device class denied");expect(j["sensorClass"]["open"]["win32Error"]==ERROR_ACCESS_DENIED&&j["sensorClass"]["enumerationComplete"]==false&&ops.enumCalls==0,"class access denied remains failed, never a successful empty list");}
 {DeviceEvidenceOps ops;ops.count=3;ops.failEnumAt=1;const auto j=deviceSnapshot(ops,"device enum partial");expect(j["sensorClass"]["nodes"].size()==1&&j["sensorClass"]["enumerationEnd"]["win32Error"]==ERROR_ACCESS_DENIED&&j["sensorClass"]["enumerationComplete"]==false,"mid-enumeration error retains the earlier node and incomplete coverage");}
 {DeviceEvidenceOps ops;ops.denyProperty=true;ops.failNodeStatus=true;const auto j=deviceSnapshot(ops,"device properties partial");const auto& row=j["sensorClass"]["nodes"][0];
  expect(row["propertyQueriesComplete"]==true&&row["metadataComplete"]==false&&row["properties"]["manufacturer"]["win32Error"]==ERROR_ACCESS_DENIED,"attempt coverage is distinct from successful complete node metadata");
  expect(row["statusQuery"]["configRet"]==CR_ACCESS_DENIED&&row["statusQuery"]["problem"].is_null()&&row["statusQuery"]["rawProblem"]==7,"failed CM status retains raw results without promoting event-like code7 into a proven problem code");
  expect(row["parents"][0]["properties"]["manufacturer"]["configRet"]==CR_ACCESS_DENIED&&row["parents"][0]["properties"]["manufacturer"]["win32Error"].is_null(),"parent property CONFIGRET is not relabelled as Win32");
 }
 {DeviceEvidenceOps ops;ops.wrongType=true;ops.badBool=true;ops.badString=true;const auto j=deviceSnapshot(ops,"device malformed metadata");const auto& p=j["sensorClass"]["nodes"][0]["properties"];
  expect(p["problemStatus"]["status"]=="type-mismatch"&&p["problemStatus"]["value"].is_null(),"UINT32 cannot masquerade as NTSTATUS");
  expect(p["isPresent"]["status"]=="malformed"&&p["isPresent"]["value"].is_null()&&p["description"]["value"].is_null(),"invalid BOOL and unterminated UTF16 stay unknown");
 }
 {DeviceEvidenceOps ops;ops.hugeProperty=true;const auto j=deviceSnapshot(ops,"device property buffer cap");const auto& p=j["sensorClass"]["nodes"][0]["properties"]["hardwareIds"];expect(p["truncated"]==true&&p["requiredBytes"]==de::max_property_bytes+1&&p["win32Error"]==ERROR_INSUFFICIENT_BUFFER&&p["value"].is_null(),"fixed property buffer retains required size and actual error on overflow");}
 {DeviceEvidenceOps ops;ops.manyIds=true;const auto j=deviceSnapshot(ops,"device array cap");const auto& p=j["sensorClass"]["nodes"][0]["properties"]["hardwareIds"];expect(p["value"].size()==de::max_array_entries&&p["truncated"]==true&&j["truncated"]==true,"hardware/compatible ID lists retain their prefix and explicit entry truncation");}
 {DeviceEvidenceOps ops;ops.parents=8;const auto j=deviceSnapshot(ops,"device parent cap");const auto& row=j["sensorClass"]["nodes"][0];expect(row["parents"].size()==6&&row["parentChain"]["truncated"]==true,"parent traversal cannot exceed six actual ancestors");}
 {DeviceEvidenceOps ops;ops.failParent=true;const auto j=deviceSnapshot(ops,"device parent failure");expect(j["sensorClass"]["nodes"][0]["parentChain"]["lastQuery"]["configRet"]==CR_ACCESS_DENIED&&j["sensorClass"]["nodes"][0]["parentChain"]["status"]=="failed","failed parent query keeps raw CR without inventing a root");}
 {DeviceEvidenceOps ops;ops.count=100;ops.parents=0;ops.bulky=true;const auto j=deviceSnapshot(ops,"device total byte and node caps");expect(j["sensorClass"]["nodes"].size()<=64&&ops.enumCalls<=64&&j["truncated"]==true&&j["sensorClass"]["enumerationComplete"]==false,"large class inventory is bounded by byte or node limits without fake completion");expect(j["byteLimitReached"]==true,"oversized metadata stops collection before the total export limit");}
 {DeviceEvidenceOps ops;ops.denyScm=true;const auto j=deviceSnapshot(ops,"device SCM denied");expect(j["services"]["open"]["win32Error"]==ERROR_ACCESS_DENIED&&j["services"]["entries"].empty(),"SCM denial retains unknown service state");}
 {DeviceEvidenceOps ops;ops.missingService=true;const auto j=deviceSnapshot(ops,"device services missing");const auto& s=j["services"]["entries"][0];expect(s["statusOpen"]["win32Error"]==ERROR_SERVICE_DOES_NOT_EXIST&&s["statusQuery"]["status"]=="NOT_COLLECTED"&&s["startTypeQuery"]["status"]=="NOT_COLLECTED","missing service is neither stopped nor disabled nor driver damage proof");}
 {DeviceEvidenceOps ops;ops.denyConfig=true;const auto j=deviceSnapshot(ops,"device config denied");const auto& s=j["services"]["entries"][0];expect(s["statusQuery"]["currentState"]==SERVICE_RUNNING&&s["configOpen"]["win32Error"]==ERROR_ACCESS_DENIED&&s["startTypeQuery"]["status"]=="NOT_COLLECTED","start-type access denial does not suppress allowed status or fabricate Disabled");}
 {DeviceEvidenceOps ops;ops.failServiceStatus=true;ops.disabledService=true;const auto j=deviceSnapshot(ops,"device failed status and disabled config");const auto& s=j["services"]["entries"][0];expect(s["statusQuery"]["win32Error"]==ERROR_ACCESS_DENIED&&s["statusQuery"]["currentState"].is_null()&&s["startTypeQuery"]["startType"]==SERVICE_DISABLED,"failed service status and observed Disabled are distinct facts");}
 for(const auto& api:{"open-class","enum","status","property","parent","scm","open-service","service-status","service-config"}){
  DeviceEvidenceOps ops;ops.slowApi=api;const auto j=deviceSnapshot(ops,std::string("device elapsed budget ")+api);
  expect(j["budgetExhausted"]==true&&j["overBudget"]==true&&j["singleSystemCallHardInterruptible"]==false&&j["collectionStatus"]=="partial",std::string("actual returning late API is honest and stops further reads: ")+api);
 }
 {DeviceEvidenceOps ops;ops.slowApi="enum";ops.slowMs=1500;const auto j=deviceSnapshot(ops,"device exact deadline");expect(j["budgetExhausted"]==true&&j["overBudget"]==false,"exact deadline exhausts budget without claiming a call exceeded it");}
 {DeviceEvidenceOps ops;ops.closeDelay=1501;const auto j=deviceSnapshot(ops,"device cleanup budget");expect(ops.closedSets==1&&j["overBudget"]==true,"mandatory RAII cleanup runs after the boundary and its elapsed time is reported");}
 {DeviceEvidenceOps ops;ops.throwProperty=true;bool threw=false;SetLastError(983);try{de::snapshot_with_ops<json>(ops);}catch(const std::runtime_error&){threw=true;}expect(threw&&ops.liveSets==0&&ops.liveServices==0&&GetLastError()==983,"collector exception releases the owned set and restores error for the export boundary");}
}
// @@PROVIDER@@
// @@TICKET@@
// @@QUERY@@
static ymcc::sensors::ThreadMailbox g_sensorThreadMailbox;
static std::function<void(unsigned)> g_sensorThreadCommandForTest;
static unsigned winrtCensuses=0;
static bool deviceChangePowerBlocked=false;
static bool inputCaptureSensorStopRequested(){return deviceChangePowerBlocked||g_sensorThreadMailbox.stopRequested();}
static void inputCaptureObserveWindowsInventory(const inv::Ticket&){++winrtCensuses;}
static void inputCaptureRunWindowsSelection(){}
static void inputCaptureRunWindowsSourceProbe(){}
static void actorTail(inv::Schedule& inventorySchedule,bool shutdown=false){
// @@ACTOR_TAIL@@
}
// Recovery executes current main.cpp helpers and caller slices, not a reimplementation.
// Only OS/source side effects and capability/ownership observations below are ports.
static ymcc::sensors::device_change::Hint g_windowsSensorDeviceChange;
static std::mutex g_sensorThreadMx;
static HANDLE g_sensorThread=nullptr,g_sensorThreadWake=nullptr;
static std::uint64_t deviceChangeNow=10000;
static ULONGLONG deviceChangeTick(){return deviceChangeNow;}
static bool g_gyroSensor=false,g_accelSensor=false;
static bool g_gyroReadingChangedSubscribed=false,g_accelReadingChangedSubscribed=false;
static std::atomic<bool> g_sensorWakePollActive{false},g_gyroJudgeTaskEnabled{false};
static std::atomic<bool> g_sensorUnsubscribeRetained{false},g_windowsSourceProbeBusy{false};
static bool deviceChangeSelectionPending=false,deviceChangeExplicitLease=false,deviceChangeLegacyNeeded=true;
static unsigned deviceChangeAdmissionCalls=0,deviceChangeColdStarts=0,deviceChangeWakeSteps=0,deviceChangeJudgeSteps=0;
static unsigned deviceChangeOtherNotifications=0;
static bool deviceChangeAdmissionUsedMissing=false,deviceChangeAppear=false,deviceChangeColdStartThrows=false;
static std::function<void()> deviceChangeAdmissionHook;
struct DeviceChangeMeta {bool gyroPresent=false,accelPresent=false;};
static DeviceChangeMeta deviceChangeMeta;
static std::vector<std::string> deviceChangeEffects;
static std::vector<json> deviceChangeLogs;
static bool inputCaptureHcWindowsLegacyNeeded(const MachineIdentity&,bool gyro,bool accel){
 ++deviceChangeAdmissionCalls;deviceChangeAdmissionUsedMissing=!gyro&&!accel;
 if(deviceChangeAdmissionHook)deviceChangeAdmissionHook();return deviceChangeLegacyNeeded;
}
static bool inputCaptureWindowsSelectionHasWork(){return deviceChangeSelectionPending||deviceChangeExplicitLease;}
static DeviceChangeMeta inputCaptureSensorMetaSnapshot(){return deviceChangeMeta;}
static void appendDomainLog(const char* domain,const char* event,const json& value){
 deviceChangeLogs.push_back({{"domain",domain},{"event",event},{"value",value}});
}
struct DeviceChangeProvider {
 bool live=false,retired=true,stopOk=true,stopRetires=true;
 unsigned stops=0;UINT32 lastStopBudget=INFINITE;
 const char* name="unset";std::function<void()> onStop;
 bool running()const{return live;}
 bool stopped()const{return retired;}
 bool stop(UINT32 budget=INFINITE){
  ++stops;lastStopBudget=budget;deviceChangeEffects.push_back(name);
  if(onStop)onStop();if(stopOk&&stopRetires)retired=true;return stopOk;
 }
};
static DeviceChangeProvider g_msiSensorApiProvider,g_hcWindowsSensorApiProvider;
static bool inputCaptureStopHcWindowsSensorApi(){return g_hcWindowsSensorApiProvider.stop();}
static void inputCaptureStartWinRtSensorsImpl(){
 ++deviceChangeColdStarts;deviceChangeEffects.push_back("coldstart");
 if(deviceChangeColdStartThrows)throw std::runtime_error("fixture-coldstart-failure");
 // Explicit late-arrival result; no WinRT call, reading, output or recovery receipt.
 if(deviceChangeAppear){g_gyroSensor=true;g_accelSensor=true;}
}
static void inputCaptureSensorWakePollStep(){++deviceChangeWakeSteps;}
static void inputCaptureSensorHighMagKeepaliveStep(){++deviceChangeJudgeSteps;}
static void sgNoteExternalDeviceNodeChange(){++deviceChangeOtherNotifications;}
static void hidHideInvalidateRawInputIdentityCache(){++deviceChangeOtherNotifications;}
static void xinputSlotsMarkDiscoveryDirty(){++deviceChangeOtherNotifications;}
static void rogRemapNoteDeviceTreeChange(){++deviceChangeOtherNotifications;}
static void msiDinputNoteDeviceTreeChange(){++deviceChangeOtherNotifications;}
// @@DEVICE_CONSTANTS@@
// GetTickCount64 is the explicit deterministic OS clock port, scoped to these slices.
// The unsignalled thread event models a live handle; WaitForSingleObject(0)/SetEvent are real.
#define GetTickCount64 deviceChangeTick
// @@REDISCOVER@@
// @@NOTE_DEVICE_CHANGE@@
static UINT32 deviceChangeActorWait(){
// @@ACTOR_WAIT@@
 return waitMs;
}
static void deviceChangeActorIdle(bool shutdown=false,bool hadRequest=false){
// @@ACTOR_IDLE@@
}
static void deviceChangeWndProc(WPARAM w){
// @@WM_DEVICE_CHANGE_PREDICATE@@
// @@WM_DEVICE_CHANGE_BRANCH@@
}
// Only the actual first reset statement is compiled here; the remaining unsubscribe/
// power callback effects are outside this fixture and never run on the host.
static void deviceChangeUnsubscribeReset(){
// @@UNSUBSCRIBE_RESET@@
}
static void deviceChangePowerPauseReset(){
// @@POWER_PAUSE_RESET@@
}
#undef GetTickCount64
static std::atomic<unsigned> factories{0};static std::vector<std::shared_ptr<Shared>> allStates;
static ci::Census::Factory factory(std::shared_ptr<Shared> state){allStates.push_back(state);return [state](ISensorManager** out){++factories;if(state->factoryDenied){*out=nullptr;return E_ACCESSDENIED;}*out=new Manager(state);return S_OK;};}
static inv::Ticket fresh(){ci::Census::cancel();ticketHook={};power=7;g_inputCaptureSink=std::make_shared<Sink>();return inputCaptureWindowsInventoryTicket();}
static bool settled(const inv::Ticket& t){return await([&]{return !ci::Census::inspect(t,GetTickCount64()).workerRunning;},2500);}
static json run(const std::shared_ptr<Shared>& state,const std::string& name){const auto t=fresh();const auto before=ci::Census::inspect(t,GetTickCount64()).observation.serial;expect(ci::Census::request(t,factory(state)),name+": census starts");expect(await([&]{const auto v=ci::Census::inspect(t,GetTickCount64());return v.observation.serial>before&&!v.workerRunning;}),name+": owned worker actually finishes");auto result=inputCaptureSensorApiInventoryStatus();expect(state->gyroReads==0&&state->accelReads==0&&state->samples==0&&state->writes==0&&state->permissions==0,name+": no sample reads, writes, permissions or subscriptions");expect(state->managers==0&&state->sensors==0&&state->reports==0,name+": actual COM references drain");if(!state->factoryDenied)expect(state->createdThread!=GetCurrentThreadId()&&state->createdThread==state->destroyedThread,name+": MTA owns COM lifetime");expect(result["deviceClass"]=="fixture-original-model"&&!result["automaticSelectionAllowed"].get<bool>()&&!result["samplesPublished"].get<bool>(),name+": identity and output policy remain independent");return result;}
static bool writeOk=true;static unsigned writes=0;static std::map<std::wstring,std::string> files;
static std::wstring yemancc_data_dir(){return L"fixture";}
static std::string nativeLifecycleTimestamp(){return "fixture-time";}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& text){++writes;SetLastError(99);if(!writeOk)return false;files[path]=text;return true;}
static json inputCaptureWindowsSensorStatus(){return {{"scope","fixture-winrt"}};}
static json inputCaptureWindowsSourceProbeStatus(){return {{"scope","fixture-probe"}};}
static json inputCaptureWindowsSourceEligibilityStatus(){return {{"scope","fixture-eligibility"}};}
static json inputCaptureWindowsProcessingReceiptSnapshot(){return {{"scope","fixture-processing"}};}
static json inputCaptureWindowsSourceSelectionStatus(){return {{"scope","fixture-selection"}};}
// @@DEVICE_EXPORT@@
// @@EXPORT@@
static json packageDeviceEvidenceForTest(){json availability={{"files",json::array()}};json added=json::array();
 auto addFile=[&](const std::wstring& path){added.push_back(W2U(path));};
// @@DEVICE_PACKAGING@@
 return {{"added",added},{"availability",availability}};
}
static std::string deviceManifestForTest(){std::ostringstream manifest;
// @@DEVICE_MANIFEST@@
 return manifest.str();}
static void deviceExportCases(){
 deviceEvidenceOps=DeviceEvidenceOps{};deviceSnapshotThrows=false;writeOk=true;SetLastError(811);
 const auto path=exportWindowsSensorDeviceEvidence();expect(!path.empty()&&GetLastError()==811,"new actual device export preserves last error after snapshot and atomic writer");
 const auto evidence=json::parse(files.at(path));expect(evidence["schema"]==1&&evidence["nativeEvidenceVersion"]==1&&evidence["exportEntry"]=="logs.exportAll"&&evidence["observedAt"]=="fixture-time","actual device export decorates the real collector without changing cached evidence");
 const auto old=files.at(path);writeOk=false;deviceEvidenceOps=DeviceEvidenceOps{};SetLastError(812);
 expect(exportWindowsSensorDeviceEvidence().empty()&&GetLastError()==812&&files.at(path)==old,"failed device write returns no current path and cannot overwrite or reuse stale saved evidence");
 deviceEvidenceOps=DeviceEvidenceOps{};const auto missing=packageDeviceEvidenceForTest();
 expect(missing["added"].empty()&&missing["availability"]["files"][0]["name"]=="windows-sensor-devices.json"&&missing["availability"]["files"][0]["present"]==false&&missing["availability"]["files"][0]["missingReason"]=="current-windows-sensor-device-snapshot-not-saved","actual packaging omits the stale device file and records current-save failure");
 writeOk=true;deviceEvidenceOps=DeviceEvidenceOps{};const auto saved=packageDeviceEvidenceForTest();expect(saved["added"].size()==1&&saved["added"][0]==W2U(path)&&saved["availability"]["files"].empty(),"actual packaging adds only the newly saved device snapshot");
 const auto before=writes;deviceSnapshotThrows=true;SetLastError(813);expect(exportWindowsSensorDeviceEvidence().empty()&&writes==before&&GetLastError()==813,"snapshot exception cannot write or return stale evidence and preserves last error");deviceSnapshotThrows=false;
 const auto manifest=deviceManifestForTest();expect(manifest.find("windowsSensorDeviceEvidence=windows-sensor-devices.json;")!=std::string::npos&&manifest.find("explicit-export-only")!=std::string::npos&&manifest.find("empty-not-hardware-absence-proof")!=std::string::npos,"actual manifest describes the separately packed read-only explicit snapshot");
 deviceEvidenceOps=DeviceEvidenceOps{};
}
static std::map<std::string,std::function<json(const json&)>> handlers;
static void ipc_on(const char* name,std::function<json(const json&)> handler){handlers[name]=std::move(handler);}
static void registerIpc(){
// @@IPC@@
}
static void deviceChangeCases(){
 using Hint=ymcc::sensors::device_change::Hint;
 struct Events {
  HANDLE thread=CreateEventW(nullptr,TRUE,FALSE,nullptr),wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
  ~Events(){g_sensorThread=nullptr;g_sensorThreadWake=nullptr;if(thread)CloseHandle(thread);if(wake)CloseHandle(wake);}
 } events;
 expect(events.thread&&events.wake,"device change owns real fake-thread and wake events");if(!events.thread||!events.wake)return;
 auto freshChange=[&]{
  g_windowsSensorDeviceChange.reset();g_sensorThreadMailbox.close();g_sensorThreadMailbox.open();
  g_sensorThread=events.thread;g_sensorThreadWake=events.wake;ResetEvent(events.thread);ResetEvent(events.wake);
  g_sensorThreadCommandForTest={};ticketHook={};power=7;deviceChangeNow=10000;
  g_inputCaptureSink=std::make_shared<Sink>();g_machineIdentity=Machine{};g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::OneXPlayer;
  deviceChangePowerBlocked=false;deviceChangeLegacyNeeded=true;deviceChangeAdmissionHook={};
  deviceChangeAdmissionCalls=deviceChangeColdStarts=deviceChangeWakeSteps=deviceChangeJudgeSteps=deviceChangeOtherNotifications=0;
  deviceChangeAdmissionUsedMissing=deviceChangeAppear=deviceChangeColdStartThrows=false;
  deviceChangeSelectionPending=deviceChangeExplicitLease=false;g_windowsSourceProbeBusy=false;
  g_sensorWakePollActive=false;g_gyroJudgeTaskEnabled=false;g_sensorUnsubscribeRetained=false;
  g_gyroSensor=g_accelSensor=g_gyroReadingChangedSubscribed=g_accelReadingChangedSubscribed=false;deviceChangeMeta={};
  g_hcWindowsSensorApiProvider=DeviceChangeProvider{};g_hcWindowsSensorApiProvider.name="hc-stop";
  g_msiSensorApiProvider=DeviceChangeProvider{};g_msiSensorApiProvider.name="msi-stop";
  deviceChangeEffects.clear();deviceChangeLogs.clear();
 };
 auto event=[&]{deviceChangeWndProc(DBT_DEVNODES_CHANGED);};
 auto due=[&]{event();deviceChangeNow+=250;};
 auto noReplacement=[&](const std::string& reason){
  expect(deviceChangeColdStarts==0,reason+": original coldstart port is never invoked");
  expect(g_hcWindowsSensorApiProvider.stops==0&&g_msiSensorApiProvider.stops==0,reason+": existing Provider owners are not touched");
 };
 expect(Hint::mergeMs==250&&Hint::minAttemptIntervalMs==2000,"event timing matches independent 250ms merge and 2s attempt requirements");
 {freshChange();for(unsigned i=0;i<20;++i){deviceChangeNow+=10000;deviceChangeActorIdle();inputCaptureRediscoverMissingWindowsSensors();}
  noReplacement("no OS device event");expect(deviceChangeActorWait()==INFINITE&&deviceChangeLogs.empty(),"no event creates no periodic recovery wait or synthetic hint");}
 {freshChange();for(WPARAM code:{WPARAM{DBT_DEVICEARRIVAL},WPARAM{DBT_DEVICEREMOVECOMPLETE},WPARAM{DBT_QUERYCHANGECONFIG},WPARAM{0}})deviceChangeWndProc(code);
  expect(WaitForSingleObject(events.wake,0)==WAIT_TIMEOUT&&deviceChangeActorWait()==INFINITE&&deviceChangeOtherNotifications==0,"actual WM retry predicate ignores non-devnode notifications");
  deviceChangeAppear=true;event();expect(deviceChangeColdStarts==0&&deviceChangeEffects.empty(),"WndProc only queues a hint; discovery and cleanup never run there");
  expect(WaitForSingleObject(events.wake,0)==WAIT_OBJECT_0&&WaitForSingleObject(events.wake,0)==WAIT_TIMEOUT,"actual note signals the owned auto-reset STA wake event once");
  expect(deviceChangeOtherNotifications==5,"actual WM retry branch retains its five unrelated notification ports");
  expect(deviceChangeActorWait()==250,"actual actor formula shortens infinite idle wait to event merge deadline");
  deviceChangeNow+=249;deviceChangeActorIdle();expect(deviceChangeColdStarts==0&&deviceChangeActorWait()==1,"actual idle cannot discover before the merge deadline");
  ++deviceChangeNow;deviceChangeActorIdle();
  expect(deviceChangeColdStarts==1&&g_gyroSensor&&g_accelSensor,"late appearance executes the original coldstart port exactly once and observes its stub objects");
  expect(deviceChangeEffects==std::vector<std::string>{"hc-stop","msi-stop","coldstart"},"actual helper proves old owners returned before its single coldstart");
  expect(g_msiSensorApiProvider.lastStopBudget==0&&deviceChangeAdmissionUsedMissing,"actual helper requests nonblocking MSI retirement and total-library admission for a missing pair");
  expect(deviceChangeActorWait()==INFINITE,"claimed hint leaves no repeating actor timer");
  for(unsigned i=0;i<10;++i){deviceChangeNow+=5000;deviceChangeActorIdle();}expect(deviceChangeColdStarts==1,"successful stub discovery never creates a periodic rediscovery loop");
  const auto logCount=deviceChangeLogs.size();due();deviceChangeActorIdle();expect(deviceChangeColdStarts==1&&deviceChangeLogs.size()>logCount,"a later event preserves the now-existing source");
  for(const auto& row:deviceChangeLogs){const auto& value=row["value"];expect(!value.value("hardwareReady",false)&&!value.value("completed",false),"event/discovery diagnostics never claim physical recovery or a completed source receipt");}
 }
 {freshChange();event();expect(WaitForSingleObject(events.wake,0)==WAIT_OBJECT_0,"storm first hint wakes STA");
  for(unsigned i=0;i<249;++i){++deviceChangeNow;event();}
  expect(deviceChangeActorWait()==1&&WaitForSingleObject(events.wake,0)==WAIT_TIMEOUT,"same-scope event storm merges without extending deadline or re-signalling wake");
  ++deviceChangeNow;deviceChangeActorIdle();expect(deviceChangeColdStarts==1,"storm produces one coldstart attempt at the first 250ms deadline");
  event();expect(deviceChangeActorWait()==2000,"next actual event respects 2s minimum from the previous attempt");
  deviceChangeNow+=1999;deviceChangeActorIdle();expect(deviceChangeColdStarts==1&&deviceChangeActorWait()==1,"throttled event cannot attempt one millisecond early");
  ++deviceChangeNow;deviceChangeActorIdle();expect(deviceChangeColdStarts==2&&deviceChangeActorWait()==INFINITE,"throttled event is consumed once at its exact deadline");
  for(unsigned i=0;i<10;++i){deviceChangeNow+=10000;deviceChangeActorIdle();}expect(deviceChangeColdStarts==2,"failed discovery does not poll again without a new OS event");
 }
 for(const std::string healthy:{"gyro-object","accel-object","gyro-subscription","accel-subscription","gyro-present","accel-present","msi-running","hc-running","unsubscribe-retained"}){
  freshChange();
  if(healthy=="gyro-object")g_gyroSensor=true;if(healthy=="accel-object")g_accelSensor=true;
  if(healthy=="gyro-subscription")g_gyroReadingChangedSubscribed=true;if(healthy=="accel-subscription")g_accelReadingChangedSubscribed=true;
  if(healthy=="gyro-present")deviceChangeMeta.gyroPresent=true;if(healthy=="accel-present")deviceChangeMeta.accelPresent=true;
  if(healthy=="msi-running")g_msiSensorApiProvider.live=true;if(healthy=="hc-running")g_hcWindowsSensorApiProvider.live=true;
  if(healthy=="unsubscribe-retained")g_sensorUnsubscribeRetained=true;
  due();deviceChangeActorIdle();noReplacement("healthy/owned "+healthy);
  expect(deviceChangeActorWait()==INFINITE,"healthy/owned hint is consumed without a timer: "+healthy);
 }
 for(const auto family:{YmccFamily::Unknown,YmccFamily::MsiClaw,YmccFamily::Ayanero,YmccFamily::Lenovo,YmccFamily::Asus,YmccFamily::Gpd}){
  freshChange();g_machineIdentity.family=family;due();deviceChangeActorIdle();noReplacement("other family "+std::to_string(static_cast<int>(family)));
  expect(deviceChangeAdmissionCalls==0,"other family short-circuits even a permissive capability port");
 }
 {freshChange();g_machineIdentity.resolved=false;due();deviceChangeActorIdle();noReplacement("unresolved OneX");expect(deviceChangeAdmissionCalls==0,"unresolved identity cannot reach capability admission");}
 {freshChange();deviceChangeLegacyNeeded=false;due();deviceChangeActorIdle();noReplacement("OneX absent from WindowsLegacyNeeded");expect(deviceChangeAdmissionCalls==1&&deviceChangeAdmissionUsedMissing,"resolved OneX still requires total-library admission");}
 for(const std::string owner:{"selection-pending","explicit-lease","probe","wake-recovery"}){
  freshChange();if(owner=="selection-pending")deviceChangeSelectionPending=true;if(owner=="explicit-lease")deviceChangeExplicitLease=true;
  if(owner=="probe")g_windowsSourceProbeBusy=true;if(owner=="wake-recovery")g_sensorWakePollActive=true;
  due();deviceChangeActorIdle();noReplacement("existing "+owner);
  if(owner=="wake-recovery")expect(deviceChangeWakeSteps==1,"actual idle continues the original wake recovery port without starting another discovery");
 }
 for(const std::string change:{"binding","power","unknown-power","owner","inactive-binding","retired-owner"}){
  freshChange();event();
  if(change=="binding")++g_inputCaptureSink->activeBindingGeneration;if(change=="power")++power;if(change=="unknown-power")power=0;
  if(change=="owner")g_inputCaptureSink=std::make_shared<Sink>();if(change=="inactive-binding")g_inputCaptureSink->activeBindingGeneration=0;if(change=="retired-owner")g_inputCaptureSink.reset();
  deviceChangeNow+=250;expect(deviceChangeActorWait()==INFINITE,"actual wait expires a stale hint for "+change);deviceChangeActorIdle();noReplacement("stale scope "+change);
 }
 {freshChange();std::weak_ptr<Sink> weak=g_inputCaptureSink;event();g_inputCaptureSink.reset();expect(weak.expired(),"queued event Hint and its throttle do not retain a sensor owner");
  deviceChangeNow+=250;inputCaptureRediscoverMissingWindowsSensors();expect(deviceChangeColdStarts==0&&deviceChangeActorWait()==INFINITE,"an expired weak owner cannot be revived by a late hint");}
 {freshChange();due();deviceChangeActorIdle();std::weak_ptr<Sink> weak=g_inputCaptureSink;g_inputCaptureSink.reset();
  expect(weak.expired(),"claimed Hint throttle also keeps only a weak reference to its former owner");}
 for(const std::string invalid:{"power","binding"}){
  freshChange();if(invalid=="power")power=0;else g_inputCaptureSink->activeBindingGeneration=0;
  event();expect(WaitForSingleObject(events.wake,0)==WAIT_TIMEOUT&&deviceChangeLogs.empty(),"WndProc rejects an invalid posting scope: "+invalid);
 }
 {freshChange();due();g_sensorThreadMailbox.close();inputCaptureRediscoverMissingWindowsSensors();g_sensorThreadMailbox.open();deviceChangeActorIdle();
  noReplacement("closed inbox");expect(deviceChangeActorWait()==INFINITE,"closed inbox clears its old hint before reopening");}
 {freshChange();event();deviceChangeAdmissionHook=[]{g_inputCaptureSink=std::make_shared<Sink>();};deviceChangeNow+=250;deviceChangeActorIdle();
  noReplacement("owner changes during capability admission");}
 for(const std::string change:{"power","binding","owner","stop","suspend","shutdown","start","selection","lease","probe","power-block"}){
  freshChange();g_hcWindowsSensorApiProvider.retired=false;
  g_hcWindowsSensorApiProvider.onStop=[change]{
   if(change=="power")++power;if(change=="binding")++g_inputCaptureSink->activeBindingGeneration;if(change=="owner")g_inputCaptureSink=std::make_shared<Sink>();
   if(change=="stop")g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Stop);
   if(change=="suspend")g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Suspended);
   if(change=="shutdown")g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Shutdown);
   if(change=="start")g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Start);
   if(change=="selection")deviceChangeSelectionPending=true;if(change=="lease")deviceChangeExplicitLease=true;
   if(change=="probe")g_windowsSourceProbeBusy=true;if(change=="power-block")deviceChangePowerBlocked=true;
  };
  due();deviceChangeActorIdle();expect(deviceChangeColdStarts==0&&g_hcWindowsSensorApiProvider.stops==1,"post-retirement gate rejects a raced "+change);
 }
 for(const auto terminal:{ymcc::sensors::ThreadRequest::Stop,ymcc::sensors::ThreadRequest::Suspended,ymcc::sensors::ThreadRequest::Shutdown}){
  freshChange();due();const auto posted=g_sensorThreadMailbox.submit(terminal);deviceChangeActorIdle();noReplacement("queued terminal command");
  inputCaptureRediscoverMissingWindowsSensors();const auto taken=g_sensorThreadMailbox.take();expect(posted&&taken==posted,"recovery preserves original terminal request ownership");
  g_sensorThreadMailbox.complete(taken);g_sensorThreadMailbox.close();g_sensorThreadMailbox.open();deviceChangeActorIdle();
  expect(deviceChangeColdStarts==0&&deviceChangeActorWait()==INFINITE,"queued Stop/Suspend/Shutdown clears pending recovery and cannot resurrect after inbox reopens");
 }
 {freshChange();due();deviceChangePowerBlocked=true;deviceChangeActorIdle();inputCaptureRediscoverMissingWindowsSensors();deviceChangePowerBlocked=false;deviceChangeActorIdle();
  noReplacement("power-blocked helper");expect(deviceChangeActorWait()==INFINITE,"power-blocked recovery clears the hint rather than postponing resurrection");}
 for(const std::string rejected:{"closed","missing-thread","missing-wake","exited-thread","queued-stop"}){
  freshChange();if(rejected=="closed")g_sensorThreadMailbox.close();if(rejected=="missing-thread")g_sensorThread=nullptr;
  if(rejected=="missing-wake")g_sensorThreadWake=nullptr;if(rejected=="exited-thread")SetEvent(events.thread);
  if(rejected=="queued-stop")g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Stop);
  event();expect(WaitForSingleObject(events.wake,0)==WAIT_TIMEOUT&&deviceChangeLogs.empty(),"nonblocking WndProc note rejects "+rejected+" before hint/wake");
 }
 for(const std::string failure:{"hc-stop-false","msi-stop-false","hc-not-retired","msi-not-retired"}){
  freshChange();g_hcWindowsSensorApiProvider.retired=false;g_msiSensorApiProvider.retired=false;
  if(failure=="hc-stop-false")g_hcWindowsSensorApiProvider.stopOk=false;if(failure=="msi-stop-false")g_msiSensorApiProvider.stopOk=false;
  if(failure=="hc-not-retired")g_hcWindowsSensorApiProvider.stopRetires=false;if(failure=="msi-not-retired")g_msiSensorApiProvider.stopRetires=false;
  due();deviceChangeActorIdle();expect(deviceChangeColdStarts==0&&g_hcWindowsSensorApiProvider.stops==1,"unproven Provider return prevents replacement: "+failure);
  if(failure=="hc-stop-false")expect(g_msiSensorApiProvider.stops==0,"failed first return short-circuits other retirement and coldstart");
  deviceChangeNow+=20000;deviceChangeActorIdle();expect(g_hcWindowsSensorApiProvider.stops==1&&deviceChangeColdStarts==0,"failed return does not trigger an unrequested retry: "+failure);
 }
 {freshChange();g_hcWindowsSensorApiProvider.retired=false;g_msiSensorApiProvider.retired=false;due();deviceChangeActorIdle();
  expect(deviceChangeEffects==std::vector<std::string>{"hc-stop","msi-stop","coldstart"}&&g_hcWindowsSensorApiProvider.retired&&g_msiSensorApiProvider.retired,"failed-but-retained Providers are actually retired through their ports before coldstart");}
 for(const std::string guard:{"shutdown","had-request","fault-hook"}){
  freshChange();due();if(guard=="fault-hook")g_sensorThreadCommandForTest=[](unsigned){};
  deviceChangeActorIdle(guard=="shutdown",guard=="had-request");noReplacement("actual idle guard "+guard);
  g_sensorThreadCommandForTest={};deviceChangeActorIdle();expect(deviceChangeColdStarts==1,"idle deferral keeps a genuine event consumable after "+guard+" clears");
 }
 {freshChange();g_gyroJudgeTaskEnabled=true;expect(deviceChangeActorWait()==1000,"actual wait keeps original highMag interval without hints");
  g_sensorWakePollActive=true;expect(deviceChangeActorWait()==2000,"actual wait keeps original wake interval without hints");
  g_sensorWakePollActive=false;deviceChangeSelectionPending=true;expect(deviceChangeActorWait()==16,"actual wait preserves explicit-selection priority");
  deviceChangeSelectionPending=false;event();expect(deviceChangeActorWait()==250,"event merges against the existing judge wait");
  deviceChangePowerBlocked=true;expect(deviceChangeActorWait()==INFINITE,"power-blocked formula does not create recovery wake polling");
  deviceChangePowerBlocked=false;deviceChangeActorIdle();expect(deviceChangeJudgeSteps==1&&deviceChangeColdStarts==0,"actual idle preserves the highMag port before event deadline");
 }
 {freshChange();auto state=std::make_shared<Shared>();state->blockEnumeration=true;const auto scope=inputCaptureWindowsInventoryTicket();
  expect(ci::Census::request(scope,factory(state)),"wait-formula case starts only the original fake-COM metadata census");
  const bool entered=WaitForSingleObject(state->readEntered,1500)==WAIT_OBJECT_0;expect(entered,"wait-formula fake metadata worker reaches controlled COM port");
  expect(deviceChangeActorWait()==INFINITE,"an active metadata worker alone does not introduce polling");
  ci::Census::cancel();const auto beforeFactories=factories.load();
  expect(!ci::Census::request(scope,factory(state))&&factories==beforeFactories&&ci::Census::pending(),"actual retry behind a cancelled live metadata worker marks pending without stacking another worker");
  expect(deviceChangeActorWait()==100,"actual formula preserves 100ms metadata wait without a device event");
  event();expect(deviceChangeActorWait()==100,"metadata wait and device hint share one minimum deadline");deviceChangeNow+=220;
  expect(deviceChangeActorWait()==30,"nearby device deadline wins over the existing metadata wait");
  deviceChangePowerBlocked=true;expect(deviceChangeActorWait()==INFINITE,"queued power admission overrides metadata and hint polling");deviceChangePowerBlocked=false;
  SetEvent(state->release);expect(settled(scope),"wait-formula fake metadata worker drains through original owner");ci::Census::cancel();
  expect(state->managers==0&&state->sensors==0&&state->samples==0,"wait-formula test has balanced fake COM and no physical samples");
 }
 for(bool powerReset:{false,true}){
  freshChange();due();deviceChangeActorIdle();expect(deviceChangeColdStarts==1,"reset case establishes one genuine attempted event");event();
  if(powerReset)deviceChangePowerPauseReset();else deviceChangeUnsubscribeReset();
  expect(deviceChangeActorWait()==INFINITE,"actual unsubscribe/power prefix cancels pending hints");event();
  expect(deviceChangeActorWait()==2000,"actual reset retains previous attempt throttle for the same scope");
  deviceChangeNow+=1999;deviceChangeActorIdle();expect(deviceChangeColdStarts==1,"reset cannot bypass 2s minimum attempt interval");
  ++deviceChangeNow;deviceChangeActorIdle();expect(deviceChangeColdStarts==2,"a fresh post-reset event may attempt once after throttle expires");
 }
 {freshChange();deviceChangeColdStartThrows=true;due();deviceChangeActorIdle();
  expect(deviceChangeColdStarts==1&&!deviceChangeLogs.empty()&&deviceChangeLogs.back()["value"]["phase"]=="discovery-exception","actual noexcept recovery catches a failing coldstart port without claiming success");
  deviceChangeNow+=20000;deviceChangeActorIdle();expect(deviceChangeColdStarts==1,"caught coldstart failure does not create a repeating recovery task");}
 {freshChange();Hint hint;const auto scope=inputCaptureWindowsInventoryTicket();
  expect(!hint.note({},10000)&&hint.waitMs({},10000,1000)==1000,"raw hint rejects an absent owner/power ticket");
  expect(hint.note(scope,10000)&&!hint.claim(scope,10249)&&hint.claim(scope,10250).has_value(),"actual header independently consumes an event only at its 250ms deadline");
  expect(!hint.claim(scope,20000)&&hint.waitMs(scope,20000,INFINITE)==INFINITE,"actual header cannot mint work without another event");
 }
 g_windowsSensorDeviceChange.reset();g_sensorThreadMailbox.close();g_sensorThreadCommandForTest={};ticketHook={};
 deviceChangePowerBlocked=false;g_machineIdentity=Machine{};ci::Census::factoryForTest()={};ci::Census::cancel();
 expect(settled({}),"recovery fixture leaves no metadata worker alive");
}
int main(int argc,char** argv){if(argc!=2)return 2;
 deviceEvidenceCases();deviceExportCases();
 auto trap=std::make_shared<Shared>();ci::Census::factoryForTest()=factory(trap);
 {const auto count=factories.load();for(unsigned i=0;i<20;++i){const auto q=inputCaptureSensorApiInventoryStatus();expect(q["recorded"]==false&&q["lastObservation"].is_null(),"unobserved SDK census remains unknown");}expect(factories==count,"cache-only queries cannot create a COM worker");ci::Census::cancel();settled(inputCaptureWindowsInventoryTicket());}
 {auto s=std::make_shared<Shared>();s->ambiguous=true;const auto q=run(s,"multiple SDK objects");const auto& b=q["lastObservation"];expect(q["metadataContextCurrent"]==true&&b["enumerationComplete"]==true&&b["reportedTotal"]==3,"all SDK sensor objects, including extra gyro, are enumerated");expect(b["observedGyroCandidates"]==2&&b["observedAccelCandidates"]==1,"multiple kinds retain actual candidate counts");expect(b["candidates"][0]["sourceId"].get<std::string>().find("sensorapi:{")==0,"GUID IDs remain API-qualified");expect(b["candidates"][0]["devicePath"]!=b["candidates"][0]["sourceId"],"provider path is not invented from sensor GUID");expect(b["candidates"][0]["standardGyroFieldsAdvertised"]==true&&b["candidates"][0]["actualStandardLayoutDecoded"]==false,"advertised fields are not reported as actual decoded samples");const auto count=factories.load();expect(!ci::Census::request(inputCaptureWindowsInventoryTicket(),factory(s)),"completed same scope is not enumerated repeatedly");expect(factories==count,"same-scope census has no repeated COM activation");}
 {auto s=std::make_shared<Shared>();s->combined=true;const auto b=run(s,"combined SDK object")["lastObservation"];expect(b["reportedTotal"]==1&&b["observedGyroCandidates"]==1&&b["observedAccelCandidates"]==1,"one real SDK object can advertise both kinds without duplicate synthetic objects");}
 {auto s=std::make_shared<Shared>();s->ambiguous=true;s->duplicateIds=true;const auto b=run(s,"duplicate IDs")["lastObservation"];expect(b["candidates"][0]["duplicateGuid"]==true&&b["candidates"][1]["duplicateGuid"]==true,"duplicate GUIDs remain explicit");expect(b["providers"][0]["duplicateIds"]==true&&b["providers"][0]["physicalPairProven"]==false,"provider grouping is not physical pairing proof");}
 {auto s=std::make_shared<Shared>();s->noSensors=true;const auto b=run(s,"observed empty SDK collection")["lastObservation"];expect(b["reportedTotal"]==0&&b["enumerationComplete"]==true&&b["candidates"].empty(),"real successful zero is distinct from unknown");}
 {auto s=std::make_shared<Shared>();s->factoryDenied=true;const auto q=run(s,"manager access denied");expect(q["lastObservation"]["reportedTotal"].is_null()&&q["lastObservation"]["status"]=="failed","failed manager enumeration cannot claim zero sensors");expect(q["candidateGroupsFromCurrentContext"].is_null(),"failed census cannot supply current candidates");}
 {auto s=std::make_shared<Shared>();s->denied=true;const auto b=run(s,"sensor denied state")["lastObservation"];expect(b["candidates"][0]["sensorState"]==static_cast<int>(SENSOR_STATE_ACCESS_DENIED)&&b["candidates"][0]["sampleReadabilityProven"]==false,"SDK access state never implies readable samples");}
 {auto s=std::make_shared<Shared>();s->unknownKind=true;const auto b=run(s,"unknown supported fields")["lastObservation"];expect(b["entriesWithUnknownKindEvidence"]==2&&b["observedCandidateCountsAreLowerBounds"]==true,"unknown metadata does not become a proven zero candidate count");expect(b["candidates"][0]["declaredGyroType"].is_null()&&b["candidates"][0]["standardGyroFieldsAdvertised"].is_null(),"failed type and field queries remain unknown");}
 {auto s=std::make_shared<Shared>();s->failGetAt=true;const auto q=run(s,"partial collection");expect(q["lastObservation"]["status"]=="partial"&&q["lastObservation"]["unavailableEntries"]==1&&q["lastObservation"]["reportedTotal"]==2,"collection entry failure retains actual reported total and partial evidence");expect(q["candidateGroupsFromCurrentContext"].is_null(),"partial enumeration does not masquerade as a complete current list");}
 {auto s=std::make_shared<Shared>();s->many=true;const auto b=run(s,"bounded collection")["lastObservation"];expect(b["reportedTotal"]==300&&b["candidates"].size()==256&&b["truncated"]==true&&b["status"]=="truncated","large collections are bounded and keep original count");}
 {auto s=std::make_shared<Shared>();s->zeroId=true;const auto b=run(s,"invalid zero GUID")["lastObservation"];expect(b["candidates"][0]["sourceId"].is_null()&&b["enumerationComplete"]==false,"zero GUID cannot become a usable source identity");}
 {auto s=std::make_shared<Shared>();s->longPath=true;const auto b=run(s,"bounded provider text")["lastObservation"];expect(b["candidates"][0]["devicePathTruncated"]==true&&b["candidates"][0]["devicePath"].get<std::string>().size()==4096&&b["candidates"][0]["providerKey"].is_null(),"truncated path cannot invent a provider association");}
 {auto s=std::make_shared<Shared>();run(s,"scope provenance");const auto original=inputCaptureSensorApiInventoryStatus();const auto originalSerial=original["observationSerial"];
  for(const std::string change:{"power","binding","owner","inactive","unknown-power"}){power=7;g_inputCaptureSink->activeBindingGeneration=73;if(change=="power")power=8;if(change=="binding")g_inputCaptureSink->activeBindingGeneration=74;if(change=="owner")g_inputCaptureSink=std::make_shared<Sink>();if(change=="inactive")g_inputCaptureSink->activeBindingGeneration=0;if(change=="unknown-power")power=0;const auto q=inputCaptureSensorApiInventoryStatus();expect(q["metadataContextCurrent"]==false&&q["candidateGroupsFromCurrentContext"].is_null(),"retired metadata is not current: "+change);expect(q["observationSerial"]==originalSerial&&q["observationBindingGeneration"]==73&&q["observationPowerGeneration"]==7,"history retains original scope: "+change);}
 }
 {auto s=std::make_shared<Shared>();run(s,"query race");unsigned n=0;ticketHook=[&]{if(++n==2)++power;};const auto q=inputCaptureSensorApiInventoryStatus();ticketHook={};expect(q["contextStableAtCheck"]==false&&q["metadataContextCurrent"]==false,"context change during actual native query is rejected");}
 {auto s=std::make_shared<Shared>();run(s,"IPC and export");registerIpc();const auto count=factories.load();const auto q=handlers.at("gyro.sensorApiInventory.get")(json::object());expect(q["scope"]=="windows-sensorapi-metadata"&&factories==count,"actual IPC handler only reads cache");SetLastError(71);const auto path=exportWindowsSensorEvidence();expect(!path.empty()&&GetLastError()==71,"actual export preserves Win32 last error");const auto evidence=json::parse(files.at(path));expect(evidence["nativeEvidenceVersion"]==3&&evidence["sensorApiInventory"]["lastObservation"]["reportedTotal"]==2,"existing Windows evidence file includes actual SDK census");expect(factories==count,"actual export cannot enumerate or sample");writeOk=false;expect(exportWindowsSensorEvidence().empty(),"failed export cannot reuse an old evidence path");writeOk=true;}
 {auto blocked=std::make_shared<Shared>();blocked->blockEnumeration=true;auto t=fresh();expect(ci::Census::request(t,factory(blocked)),"blocked metadata request starts");expect(WaitForSingleObject(blocked->readEntered,1500)==WAIT_OBJECT_0,"worker enters blocked enumeration, not GetData");const auto started=GetTickCount64();ci::Census::cancel();expect(GetTickCount64()-started<100,"cancel returns without waiting for blocked COM");auto next=std::make_shared<Shared>();const auto newTicket=fresh();const auto count=factories.load();const bool second=ci::Census::request(newTicket,factory(next));expect(!second&&factories==count,"no new worker can stack above a cancelled but live metadata call");const auto v=ci::Census::inspect(newTicket,GetTickCount64());expect(v.workerRunning&&v.workerRetireRequested&&v.pendingCurrentContext,"blocked worker remains owned and pending new scope is explicit");SetEvent(blocked->release);expect(settled(newTicket),"old metadata worker exits only after actual COM call returns");if(!second){expect(ci::Census::request(newTicket,factory(next)),"pending scope can start after old actual handle completes");expect(settled(newTicket),"new metadata request completes without overlapping workers");}expect(blocked->managers==0&&next->managers==0,"blocked and replacement COM owners drain");}
 {auto blocked=std::make_shared<Shared>();blocked->blockEnumeration=true;const auto t=fresh();expect(ci::Census::request(t,factory(blocked)),"same-scope cancellation request starts");expect(WaitForSingleObject(blocked->readEntered,1500)==WAIT_OBJECT_0,"same-scope worker enters actual blocked call");ci::Census::cancel();auto replacement=std::make_shared<Shared>();const auto count=factories.load();expect(!ci::Census::request(t,factory(replacement))&&factories==count,"same-scope cancelled live worker cannot overlap a retry");expect(ci::Census::inspect(t,GetTickCount64()).pendingCurrentContext,"same-scope cancelled worker keeps retry pending instead of stranding the metadata schedule");SetEvent(blocked->release);expect(settled(t),"same-scope cancelled handle eventually exits");expect(ci::Census::request(t,factory(replacement)),"same-scope retry starts after actual old exit");expect(settled(t),"same-scope retry completes");}
 {auto blocked=std::make_shared<Shared>();blocked->blockEnumeration=true;auto t=fresh();expect(ci::Census::request(t,factory(blocked)),"over-budget worker starts");expect(WaitForSingleObject(blocked->readEntered,1500)==WAIT_OBJECT_0,"over-budget worker reaches actual COM call");Sleep(1510);const auto v=ci::Census::inspect(t,GetTickCount64());expect(v.workerRunning&&v.workerOverBudget,"elapsed budget never pretends a live thread has exited");SetEvent(blocked->release);expect(settled(t),"over-budget handle eventually signals");expect(ci::Census::inspect(t,GetTickCount64()).observation.state==inv::State::TimedOut,"late COM completion is labelled timed-out rather than complete");}
 {auto blocked=std::make_shared<Shared>();blocked->blockEnumeration=true;auto t=fresh();std::weak_ptr<Sink> old=g_inputCaptureSink;expect(ci::Census::request(t,factory(blocked)),"weak-owner request starts");expect(WaitForSingleObject(blocked->readEntered,1500)==WAIT_OBJECT_0,"weak-owner worker reaches actual COM call");t.binding.activeBinding.reset();g_inputCaptureSink.reset();expect(old.expired(),"metadata cache, queued scope and live worker do not retain a retired sensor sink");SetEvent(blocked->release);expect(settled({}),"weak-owner worker drains after release");expect(ci::Census::inspect({},GetTickCount64()).observation.state==inv::State::Retired,"retired owner cannot publish a current census");}
 {auto state=std::make_shared<Shared>();const auto t=fresh();ci::Census::factoryForTest()=factory(state);inv::Schedule schedule;g_sensorThreadMailbox.open();
  const auto posted=g_sensorThreadMailbox.submit(ymcc::sensors::ThreadRequest::Start);const auto count=factories.load();actorTail(schedule);expect(factories==count,"actual actor tail defers SDK census while a command remains queued");
  const auto active=g_sensorThreadMailbox.take();expect(active==posted,"actual actor command is retained for acknowledgement");g_sensorThreadMailbox.complete(active);actorTail(schedule);expect(settled(t)&&factories==count+1,"actual actor schedules SDK metadata only after command acknowledgement");
  actorTail(schedule);expect(factories==count+1,"actual actor keeps completed census once per scope");g_sensorThreadMailbox.close();}
 {auto state=std::make_shared<Shared>();fresh();ci::Census::factoryForTest()=factory(state);g_sensorThreadMailbox.open();inv::Schedule schedule;const auto count=factories.load();actorTail(schedule,true);expect(factories==count,"actual actor shutdown cannot start SDK census");
  g_sensorThreadCommandForTest=[](unsigned){};actorTail(schedule);expect(factories==count,"controlled thread-command route cannot accidentally enumerate SDK hardware");g_sensorThreadCommandForTest={};
  g_inputCaptureSink->activeBindingGeneration=0;actorTail(schedule);expect(factories==count&&!ci::Census::pending(),"inactive binding cancels metadata admission");g_sensorThreadMailbox.close();}
 ci::Census::factoryForTest()={};ci::Census::cancel();settled({});for(const auto& s:allStates)expect(s->gyroReads==0&&s->accelReads==0&&s->writes==0&&s->permissions==0&&s->managers==0&&s->sensors==0&&s->reports==0,"all SDK metadata transports remain inert and COM-balanced");
 const auto originalChecksBeforeDeviceChange=checks;deviceChangeCases();
 const json report={{"status",failures.empty()?"pass":"fail"},{"originalChecksBeforeDeviceChange",originalChecksBeforeDeviceChange},{"deviceChangeChecks",checks-originalChecksBeforeDeviceChange},{"actualDeviceChangeHelpers",true},{"actualActorAndWndProcDeviceChangeSlices",true},{"physicalRecoveryVerified",false},{"checks",checks},{"failures",failures},{"actualSdkMetadataCensus",true},{"actualNativeQueryAndExport",true},{"actualDeviceEvidenceTemplate",true},{"actualDeviceExportPackagingManifest",true},{"realWindowsOpsCompiledOnly",true},{"hardwareIo",false},{"normalAppStarted",false},{"newProductionDlls",json::array()}};std::ofstream(argv[1])<<report.dump(2);std::cout<<checks<<" checks / "<<failures.size()<<" failures\n";return failures.empty()?0:1;
}
