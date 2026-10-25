#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <vector>
#include "json.hpp"
#include "legion_motion_bank_evidence.h"
#include "controller_motion_processing_evidence.h"
using json=nlohmann::json;using BYTE=std::uint8_t;using USHORT=std::uint16_t;using USAGE=USHORT;using ULONGLONG=std::uint64_t;using DWORD=std::uint32_t;
enum class YmccFamily {Unknown,LenovoLegionGo,MsiClaw};enum class PowerLifecycle {Ready,Suspended};
struct Machine {YmccFamily family=YmccFamily::LenovoLegionGo;std::string deviceClass="HandheldCompanion.Devices.LegionGoTablet";};static Machine g_machineIdentity;
struct HidHidePhysicalIdentity {bool resolved=true;USHORT vendorId=0x17ef,productId=0x6182,usagePage=0xffa0,usage=1;std::string baseContainerDeviceInstanceId="device-a";};
static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
static std::mutex g_hidHidePhysicalIdentityMx,g_legionImuMx;
static ymcc::legion_motion::Lease g_legionMotion;
static ymcc::legion_motion::BankEvidence g_legionBankEvidence;
static ymcc::controller_motion::ProcessingEvidence g_legionProcessingEvidence;
static std::atomic<uint64_t> g_legionMotionProcessorLifetime{1};
static std::atomic<uint64_t> g_legionMotionCaptureEpoch{7},g_legionMotionEpochStartedAt{1000},g_hidResolveCacheGeneration{9};
static std::atomic<unsigned> g_legionMotionSensorIndex{0};static std::atomic<bool> g_inputCaptureStop{false},g_legionRawInputSubscribed{true};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};static bool g_inputCaptureReady=true;
static uint64_t fixturePower=3,fixtureTick=1002;static unsigned fixtureClockReads=0;static std::function<void()> fixtureSecondClock;
static DWORD fixtureError=71;static bool fixtureWriteOk=true;static std::map<std::wstring,std::string> fixtureFiles;static unsigned fixtureWrites=0;
static std::map<std::string,std::function<json(const json&)>> fixtureHandlers;
static uint64_t currentPowerGeneration(){return fixturePower;}
static ULONGLONG GetTickCount64(){if(++fixtureClockReads==2&&fixtureSecondClock)fixtureSecondClock();return fixtureTick;}
static DWORD GetLastError(){return fixtureError;}static void SetLastError(DWORD e){fixtureError=e;}
static std::wstring yemancc_data_dir(){return L"fixture";}static std::string nativeLifecycleTimestamp(){return "fixture-time";}
static bool sgWriteFileAtomic(const std::wstring& path,const std::string& body){++fixtureWrites;fixtureError=99;if(!fixtureWriteOk)return false;fixtureFiles[path]=body;return true;}
static void ipc_on(const char* command,std::function<json(const json&)> handler){fixtureHandlers[command]=std::move(handler);}
namespace ymcc {inline constexpr uint64_t kVirtualOutputSourceWindowMaxAgeMs=100;}
// @@CONSTANTS@@
// @@FUNCTIONS@@
static void actualIpc(){
// @@IPC@@
}
static std::vector<std::wstring> actualPackage(json& availability){std::vector<std::wstring> result;const auto addFile=[&](const auto& path){result.push_back(path);};
// @@PACKAGE@@
return result;}
static std::string actualManifest(){std::ostringstream manifest;
// @@MANIFEST@@
return manifest.str();}
static json failures=json::array();static unsigned checks=0;
static void check(bool ok,const std::string& text){++checks;if(!ok)failures.push_back(text);}
static void reset(const std::string& name="LegionGoTablet"){g_machineIdentity={YmccFamily::LenovoLegionGo,"HandheldCompanion.Devices."+name};g_hidHidePhysicalIdentity={};if(name.find("SZ")!=std::string::npos){g_hidHidePhysicalIdentity.vendorId=0x1a86;g_hidHidePhysicalIdentity.productId=0xe310;}g_legionMotion.revoke();g_legionBankEvidence={};g_legionMotionCaptureEpoch=7;g_legionMotionEpochStartedAt=1000;g_hidResolveCacheGeneration=9;g_legionMotionSensorIndex=0;g_inputCaptureStop=false;g_legionRawInputSubscribed=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputCaptureReady=true;fixturePower=3;fixtureTick=1002;fixtureClockReads=0;fixtureSecondClock={};fixtureError=71;fixtureWriteOk=true;fixtureFiles.clear();fixtureWrites=0;fixtureHandlers.clear();}
static std::array<BYTE,96> report(bool gos=false){std::array<BYTE,96> bytes{};bytes[0]=0x7e;bytes[1]=0x21;const int values[]={100,-200,300,-400,500,-600,700,-800,900,-1000,1100,-1200};for(unsigned n=0;n<(gos?6u:12u);++n){const auto at=gos?14+n*2:n<6?(n<3?35:41)+(n%3)*2:(n<9?48:54)+(n%3)*2;const unsigned w=static_cast<unsigned>(values[n])&65535u;bytes[at]=static_cast<BYTE>(gos?w:w>>8);bytes[at+1]=static_cast<BYTE>(gos?w>>8:w);}return bytes;}
static void capture(const std::array<BYTE,96>& bytes,std::size_t actual=60){legionMotionObserveReportBanks(g_hidHidePhysicalIdentity,bytes.data(),bytes.size(),actual,1001,{3,9},7,legionMotionSelectedSensorIndex());}
static json status(){fixtureClockReads=0;return inputCaptureControllerSensorBanksStatus();}
static void seedSelected(){const auto selected=legionMotionSourceForSelectedLocked();const ymcc::legion_motion::Sample sample{{111,222,333},{.1,.2,.3},1001,{3,9},selected};check(legionMotionPublish(sample,7),"baseline selected lease seeded");}
int main(int argc,char**argv){try{
 reset();auto data=status();check(data["implemented"]==true&&data["bankCount"]==2&&data["banks"].size()==2,"two Tablet banks exposed in empty read-only query");check(data["banks"][0]["sampleObserved"]==false&&data["banks"][1]["lastCompleteSample"].is_null(),"unobserved banks are unknown, never fabricated zeros");
 for(const std::string model:{"LegionGoTablet","LegionGoTablet2","LegionGoSZ1","LegionGoSZ2"}){
  reset(model);const bool gos=model.find("SZ")!=std::string::npos;const auto bytes=report(gos);seedSelected();capture(bytes,gos?26:60);data=status();
  check(data["banks"].size()==(gos?1u:2u)&&data["banks"][0]["sampleCurrent"]==true,"current qualified model data: "+model);
  const auto original=g_legionMotion.current({3,9},legionMotionSourceForSelectedLocked(),true,1002,100);check(original&&original->gyro[0]==111&&original->gyro[1]==222,"diagnostic banks never overwrite selected output lease: "+model);
  const auto expected=ymcc::legion_input::decodeLogical(gos?ymcc::legion_input::Protocol::GoS:ymcc::legion_input::Protocol::Tablet,bytes.data(),bytes.size(),gos?26:60,0);check(expected&&data["banks"][0]["lastCompleteSample"]["gyro"][0]==expected->gx,"actual source decoder supplies observed axes: "+model);
  if(!gos){check(data["banks"][1]["sampleCurrent"]==true&&data["banks"][1]["selectedForOutput"]==false,"nonselected right data observed independently: "+model);const auto right=ymcc::legion_input::decodeLogical(ymcc::legion_input::Protocol::Tablet,bytes.data(),bytes.size(),60,1);check(data["banks"][1]["lastCompleteSample"]["gyro"][0]==right->gx&&data["banks"][0]["lastCompleteSample"]["gyro"]!=data["banks"][1]["lastCompleteSample"]["gyro"],"right report is independently decoded, not copied left: "+model);}
  check(data["observerChangesSelection"]==false&&data["observerProcessesGamepadMotion"]==false&&data["observerAppliesCalibration"]==false&&data["observerPublishesHostFrames"]==false,"raw observation cannot claim processing or output: "+model);
 }
 reset();g_legionMotionSensorIndex=1;capture(report());data=status();check(data["banks"][0]["sampleCurrent"]==true&&data["banks"][1]["selectedForOutput"]==true,"both banks observed when right is selected");
 reset();capture(report());const auto previous=status()["banks"][1]["lastCompleteSample"];capture(report(),47);data=status();check(data["banks"][0]["presentInLastObservedReport"]==true&&data["banks"][1]["presentInLastObservedReport"]==false&&data["banks"][1]["lastCompleteSample"]==previous,"short right core does not fabricate or refresh right data");
 reset();capture(report(),47);data=status();check(data["banks"][0]["sampleCurrent"]==true&&data["banks"][1]["sampleObserved"]==false,"valid 47-byte left core observes left only");
 reset();g_legionMotionSensorIndex=1;capture(report(),47);data=status();check(data["banks"][0]["sampleCurrent"]==true&&data["banks"][1]["sampleObserved"]==false,"left remains diagnosable when selected right core is absent");
 reset();auto zeros=report();for(unsigned i=35;i<60;++i)zeros[i]=0;capture(zeros);data=status();check(data["banks"][1]["sampleCurrent"]==true&&data["banks"][1]["lastCompleteSample"]["gyro"]==json::array({0.0,0.0,0.0}),"complete stationary zeros are valid observed data");
 reset();auto prefixed=report();for(unsigned i=60;i>0;--i)prefixed[i]=prefixed[i-1];prefixed[0]=0;capture(prefixed,61);data=status();check(data["lastObservedReport"]["logicalBytes"]==60&&data["banks"][1]["sampleCurrent"]==true,"native prefix normalized exactly once for both banks");
 for(const std::string change:{"power","raw","epoch","selection","physical","stop","ready","subscribed","phase","epoch-time"}){
  reset();if(change=="power")fixturePower=4;if(change=="raw")g_hidResolveCacheGeneration=10;if(change=="epoch")g_legionMotionCaptureEpoch=8;if(change=="selection")g_legionMotionSensorIndex=1;if(change=="physical")g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId="device-b";if(change=="stop")g_inputCaptureStop=true;if(change=="ready")g_inputCaptureReady=false;if(change=="subscribed")g_legionRawInputSubscribed=false;if(change=="phase")g_powerLifecycle=PowerLifecycle::Suspended;if(change=="epoch-time")g_legionMotionEpochStartedAt=1001;
  auto incoming=HidHidePhysicalIdentity{};auto bytes=report();legionMotionObserveReportBanks(incoming,bytes.data(),bytes.size(),60,1001,{3,9},7,0);data=status();check(data["lastObservedReport"].is_null()&&data["banks"][0]["sampleObserved"]==false,"stale/not-ready packet cannot establish diagnostic scope: "+change);
 }
 for(const std::string change:{"power","raw","epoch","selection","physical","stop","expired","future"}){
  reset();capture(report());if(change=="power")fixturePower=4;if(change=="raw")g_hidResolveCacheGeneration=10;if(change=="epoch")g_legionMotionCaptureEpoch=8;if(change=="selection")g_legionMotionSensorIndex=1;if(change=="physical")g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId="device-b";if(change=="stop")g_inputCaptureStop=true;if(change=="expired")fixtureTick=1102;if(change=="future")fixtureTick=999;data=status();check(data["banks"][0]["sampleCurrent"]==false&&data["banks"][1]["sampleCurrent"]==false&&data["banks"][1]["lastCompleteSample"]["powerGeneration"]==3,"history retains original context while both banks retire: "+change);
 }
 reset();capture(report());fixtureSecondClock=[]{fixturePower=4;};data=status();check(data["contextStableAtCheck"]==false&&data["banks"][1]["sampleCurrent"]==false,"query power changes invalidate bank current claims");
 reset();capture(report());fixtureSecondClock=[]{fixtureTick=1102;};data=status();check(data["banks"][0]["sampleCurrent"]==false&&data["banks"][0]["reason"]=="sample-expired-during-query","freshness is checked at query completion");
 for(const std::string wrong:{"family","model","vendor","pid","usage","unresolved","empty-container"}){reset();auto incoming=g_hidHidePhysicalIdentity;if(wrong=="family")g_machineIdentity.family=YmccFamily::MsiClaw;if(wrong=="model")g_machineIdentity.deviceClass="HandheldCompanion.Devices.Unknown";if(wrong=="vendor")incoming.vendorId=1;if(wrong=="pid")incoming.productId=1;if(wrong=="usage")incoming.usagePage=1;if(wrong=="unresolved")incoming.resolved=false;if(wrong=="empty-container")incoming.baseContainerDeviceInstanceId="";auto bytes=report();legionMotionObserveReportBanks(incoming,bytes.data(),bytes.size(),60,1001,{3,9},7,0);if(inputCaptureLegionControllerMotionRoute())check(status()["lastObservedReport"].is_null(),"wrong interface never populates banks: "+wrong);else check(status()["banks"].is_null(),"unsupported model stays explicitly unsupported: "+wrong);}
 reset();auto bytes=report();legionMotionObserveReportBanks(g_hidHidePhysicalIdentity,nullptr,0,0,1001,{3,9},7,0);legionMotionObserveReportBanks(g_hidHidePhysicalIdentity,bytes.data(),47,60,1001,{3,9},7,0);check(status()["lastObservedReport"].is_null(),"null and count-over-capacity do not invent samples");
 reset();capture(report());actualIpc();check(fixtureHandlers.at("gyro.controllerSensorBanks.get")(json::object())["banks"][1]["sampleCurrent"]==true,"actual native bank IPC delegates read-only query");json availability={{"files",json::array()}};auto paths=actualPackage(availability);check(paths.size()==1&&fixtureWrites==1&&fixtureError==71,"actual explicit export stages current banks and restores caller error");auto exported=json::parse(fixtureFiles.at(paths[0]));check(exported["exportEntry"]=="logs.exportAll"&&exported["banks"].size()==2,"actual export carries both bank observations");fixtureWriteOk=false;availability["files"]=json::array();check(actualPackage(availability).empty()&&availability["files"][0]["present"]==false,"failed bank export cannot reuse an older file");check(actualManifest().find("rawDataOnly=true;selectionAndProcessingUnchanged=true")!=std::string::npos,"actual manifest preserves raw observation scope");
 const json output={{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"failures",failures},{"actualNativeObserver",true},{"actualNativeQuery",true},{"actualIpcHandler",true},{"actualExport",true},{"actualPackageBankBlock",true},{"wholeRawInputOrExportHandlerExecuted",false},{"hardwareIo",false},{"normalAppStarted",false},{"newProductDlls",json::array()}};std::ofstream(argv[1])<<output.dump(2);std::cout<<checks<<" checks / "<<failures.size()<<" failures\n";return failures.empty()?0:1;
 }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
