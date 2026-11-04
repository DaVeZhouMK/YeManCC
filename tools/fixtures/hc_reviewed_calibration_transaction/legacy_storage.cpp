// Existing calibration consumer, legacy storage mode: no sensor, DLL or device.
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <cstdlib>
#include <cctype>
#include <vector>
#include "json.hpp"
#include "settings_baseline.h"
#include "reviewed_gyro_calibration.h"
#include "hc_model_motion_catalog.h"
using json=nlohmann::json;
namespace candidate {
enum class YmccFamily{Unknown};struct Identity{YmccFamily family{};std::string deviceClass;};static Identity g_machineIdentity;
struct View{bool privateSource=false;};static View* t_windowsProcessingView=nullptr;
static const ymcc::hc_model_motion::Recipe* inputCaptureSourceQualifiedCalibrationRecipe(){return nullptr;}
static const ymcc::hc_model_motion::Recipe* inputCaptureExtendedCalibrationRecipe(){return nullptr;}
static const ymcc::hc_model_motion::Recipe* inputCaptureReviewedCalibrationRecipe(){return nullptr;}
static json inputCaptureExtendedCalibrationSourceIdentity(const std::string&){return nullptr;}
struct InputCaptureSensorMeta{bool gyroPresent=true;std::string gyroDeviceId="fixture-legacy-device";};static InputCaptureSensorMeta g_sensorMeta;
static InputCaptureSensorMeta inputCaptureSensorMetaSnapshot(){return g_sensorMeta;}
static std::string inputCaptureCalibrationStorageKey(const std::string&);
static std::function<void()> fixtureAfterFlush;
static bool fixtureFailFlush=false;static unsigned fixtureFlushCalls=0,fixtureFailFlushAt=0;
static BOOL FlushFileBuffers(HANDLE handle){if(fixtureFailFlush||++fixtureFlushCalls==fixtureFailFlushAt){SetLastError(ERROR_WRITE_FAULT);return FALSE;}const auto result=::FlushFileBuffers(handle);if(result&&fixtureAfterFlush){auto action=std::move(fixtureAfterFlush);fixtureAfterFlush={};action();}return result;}
static std::vector<json> fixtureEvents,fixtureUiEvents;
static void appendNativeLifecycleLog(const char* name,const json& value){fixtureEvents.push_back({{"event",name},{"body",value}});}
static void gamepadSerialPostEvent(const char* name,const json& value){fixtureUiEvents.push_back({{"event",name},{"body",value}});}
// @@STORAGE_FUNCTIONS@@
static std::string inputCaptureCalibrationStorageKey(const std::string& id){return inputCaptureCalibrationKeyUpper(id);}
enum class GyroCalibStage{None,ApplyOffset,ExitManual};static std::atomic<GyroCalibStage> g_gyroCalibStageAtomic{GyroCalibStage::None};
static std::atomic<bool> g_autoCalibTrusted{false},g_autoCalibFinished{false},g_calibSourceAuto{false};
static bool g_gyroCalibMsiSession=false,g_gmProcessedThisTick=true,g_gyroCalibFreshPairThisTick=true;
static std::uint64_t g_gyroCalibSessionBinding=1,g_gyroCalibBindingThisTick=1;static std::string g_gyroCalibSessionDeviceId="fixture-legacy-device";
static unsigned g_gyroCalibProcessedTicks=1,g_gyroCalibPollWindowMs=3000;
static float g_gyroCalibConfidence=1,g_gyroCalibOx=0,g_gyroCalibOy=0,g_gyroCalibOz=0;
// @@TRUST_DECLARATION@@
static int handle=1;static void* g_gm=&handle;
static void g_gmSetOffset(void*,float,float,float,int){}
static void inputCapturePostCalibStatus(int){}
static void inputCaptureGyroCalibrationTick(){const auto autoMeta=inputCaptureSensorMetaSnapshot();switch(g_gyroCalibStageAtomic.load()){
// @@ACTUAL_APPLY_OFFSET@@
default:break;}}
}
static int checks=0;static json failures=json::array();
static void txCheck(bool ok,const std::string& label){++checks;if(!ok)failures.push_back(label);}
int main(int argc,char**argv){try{
 if(argc!=2)return 2;const auto file=std::filesystem::path(candidate::inputCaptureCalibrationJsonPath());
 const auto expected=std::filesystem::path(argv[1]).parent_path()/"isolated-appdata"/"YeManCC"/"calibration.json";
 if(std::filesystem::absolute(file).lexically_normal()!=std::filesystem::absolute(expected).lexically_normal())throw std::runtime_error("Legacy fixture cannot access real calibration path");
 std::filesystem::create_directories(file.parent_path());std::uint64_t scenarios=0;
 const auto read=[&](){json value;std::ifstream input(file);input>>value;return value;};
 const auto bytes=[&](){std::ifstream input(file,std::ios::binary);return std::string(std::istreambuf_iterator<char>(input),{});};
 const auto writeText=[&](const std::string& value){std::ofstream output(file,std::ios::binary|std::ios::trunc);output<<value;if(!output)throw std::runtime_error("Legacy fixture setup failed");};
 const auto lastResult=[&](){for(auto i=candidate::fixtureEvents.rbegin();i!=candidate::fixtureEvents.rend();++i)if((*i)["event"]=="gyro-calibration-result")return (*i)["body"];throw std::runtime_error("No actual ApplyOffset receipt");};
 // @@LEGACY_STORAGE_STORM@@
 const auto report=json{{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"scenarios",scenarios},{"failures",failures},{"actualProductionStorage",true},{"actualApplyOffsetCase",true},{"actualWin32FileApis",true},{"sourceQualification","inert-null; legacy storage only"},{"math","inert; calibration convergence not tested"},{"hardwareIo",false},{"realCalibrationFileTouched",false}};
 std::ofstream(argv[1])<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';return failures.empty()?0:1;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}}
