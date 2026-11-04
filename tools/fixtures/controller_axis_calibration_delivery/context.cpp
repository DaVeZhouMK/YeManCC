// Controlled source/window context around exact production tick/observe and UI delivery.
// No controller, settings file, virtual target, or output writer is touched.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <json.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>
using json=nlohmann::json;
static HWND g_hwnd=nullptr;
static DWORD uiThread=0;
static std::atomic<bool> g_exitRequested{false},g_inputCaptureStop{false},g_inputReady{true};
static std::atomic<std::uint64_t> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1},g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};
enum class PowerLifecycle{Ready};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::uint64_t currentPowerGeneration(){return 1;}
namespace ymcc{static constexpr std::uint64_t kVirtualOutputSourceWindowMaxAgeMs=2000;namespace touchhaptic{enum class Backend{XInput,Msi};}}
struct FeedbackSource{ymcc::touchhaptic::Backend backend=ymcc::touchhaptic::Backend::XInput;std::uint64_t generation=1;};
struct Pad{short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;unsigned char bLeftTrigger=0,bRightTrigger=0;};
struct InputCaptureGamepadSnapshot{bool live=true,readContextStable=true;std::shared_ptr<const std::string> axisCalibrationDeviceKey=std::make_shared<const std::string>("controlled-physical-source");std::uint64_t readAttemptSequence=1,readStartedTick=0,readCompletedTick=0,readPowerGeneration=1,readTopologyGeneration=1,readIdentityGeneration=1,readMsiSourceRevision=1,readMsiModeGeneration=1,readContinuityGeneration=1;FeedbackSource feedbackSource;Pad pad;};
static bool foreground=true;
static bool gamepadUiInputEligible(){return foreground;}
static std::vector<json> delivered;
static int wrongApartment=0;
static void ipc_emit(const std::string& event,const json& data={}){if(GetCurrentThreadId()!=uiThread){++wrongApartment;return;}delivered.push_back({{"event",event},{"data",data}});}
static void ipc_emit_owned_gyro_telemetry(std::string event,json data){ipc_emit(event,data);}
struct UiCostSerialScope{explicit UiCostSerialScope(const char*){}};
static void appendNativeLifecycleLog(const char*,const json&){}
/*PRODUCTION_SERIAL_TYPES_AND_POST*/
namespace axisCalibrationRuntime{
/*PRODUCTION_BINDING_AND_MATH*/
static std::mutex mx;
static std::atomic<long long> inputRevision{4};
static std::atomic<bool> centerActive{false},watchActive{false};
static std::atomic<std::uint64_t> ordinal{0};
static std::shared_ptr<const int> published=std::make_shared<const int>(1);
/*PRODUCTION_CENTER_TYPE*/
static Center center;
static std::string watchId;
static std::uint64_t lastWatchAt=0;
static InputCaptureGamepadSnapshot latest;
static json axes(const Pad& p){return{{"left",{{"x",axis(p.sThumbLX)},{"y",axis(p.sThumbLY)}}},{"right",{{"x",axis(p.sThumbRX)},{"y",axis(p.sThumbRY)}}},{"lt",p.bLeftTrigger/255.0},{"rt",p.bRightTrigger/255.0}};}
static json snapshot(){const auto b=binding(latest,GetTickCount64());return{{"protocol","ymcc.axis-calibration.v1"},{"inputRevision",4},{"inputConfigHash","native-delivery-controlled-4"},{"config",defaults()},{"source",bindingJson(b)},{"capabilities",{{"sticks","standard-independent"},{"lt","independent-analog"},{"rt","independent-analog"},{"virtualWrite","available"},{"localWrite","unsupported"}}},{"routes",{{"virtual",{{"state","bypass"},{"reason","master-disabled"},{"appliedInputRevision",nullptr},{"appliedConfigHash",nullptr},{"source",nullptr}}},{"local",{{"state","bypass"},{"reason","master-disabled"},{"appliedInputRevision",nullptr},{"appliedConfigHash",nullptr},{"source",nullptr}}}}},{"axes",{{"raw",axes(latest.pad)},{"virtual",nullptr},{"local",nullptr}}},{"sample",{{"source",bindingJson(b)},{"readSequence",std::to_string(latest.readAttemptSequence)},{"readStartedTick",std::to_string(latest.readStartedTick)},{"readCompletedTick",std::to_string(latest.readCompletedTick)}}},{"centerSession",{{"sessionId",center.id},{"active",center.active}}}};}
/*PRODUCTION_OBSERVE_AND_TICK*/
static void applySavedNotification(){const auto result=snapshot();/*PRODUCTION_APPLY_EMIT*/if(result.is_null())throw std::runtime_error("apply ACK moved away");}
}
static LRESULT CALLBACK windowProc(HWND window,UINT message,WPARAM w,LPARAM l){switch(message){/*PRODUCTION_UI_CASE*/default:return DefWindowProcW(window,message,w,l);}}
static void pump(){MSG message;while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
static void workerTick(int lx,int ly,int lt){std::thread worker([=]{auto& sample=axisCalibrationRuntime::latest;sample.pad.sThumbLX=static_cast<short>(lx);sample.pad.sThumbLY=static_cast<short>(ly);sample.pad.bLeftTrigger=static_cast<unsigned char>(lt);sample.readStartedTick=sample.readCompletedTick=GetTickCount64();sample.readAttemptSequence++;axisCalibrationRuntime::lastWatchAt=0;axisCalibrationRuntime::tick(sample);});worker.join();}
int main(int argc,char** argv){if(argc!=2)return 2;uiThread=GetCurrentThreadId();WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"AxisCalibrationDeliverySelftest";RegisterClassW(&wc);g_hwnd=CreateWindowW(wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);if(!g_hwnd)return 3;
 using namespace axisCalibrationRuntime;
 std::vector<json> checks;auto check=[&](const char* name,bool ok){checks.push_back({{"name",name},{"pass",ok}});};
 observe({{"active",true},{"watchId","watch-A"}});workerTick(16384,-8192,128);check("worker queues without calling WebView",delivered.empty()&&wrongApartment==0);pump();check("first raw axes reach UI thread",delivered.size()==1&&delivered.back()["data"]["axes"]["raw"]["left"]["x"]>0.49);
 workerTick(-16384,8192,220);pump();check("moving stick updates next frame",delivered.size()==2&&delivered.back()["data"]["axes"]["raw"]["left"]["x"]<0);
 const auto originalEvents=delivered;
 auto count=delivered.size();foreground=false;workerTick(20000,0,0);pump();check("background stays gated",delivered.size()==count);foreground=true;
 workerTick(2000,1000,0);observe({{"active",false},{"watchId","watch-A"}});observe({{"active",true},{"watchId","watch-B"}});workerTick(3000,0,0);pump();check("queued page identity stays exact",delivered.size()==count+2&&delivered[count]["data"]["observerId"]=="watch-A"&&delivered[count+1]["data"]["observerId"]=="watch-B");
 count=delivered.size();observe({{"active",false},{"watchId","watch-B"}});workerTick(4000,0,0);pump();check("stopped observer sends no frames",delivered.size()==count);
 std::thread centerWorker([&]{auto& s=latest;s.pad={};s.readStartedTick=s.readCompletedTick=GetTickCount64();center={};center.active=true;center.id="center-test";center.source=binding(s,GetTickCount64());center.revision=4;center.started=GetTickCount64();centerActive.store(true);for(int i=0;i<32;++i){Sleep(6);s.readAttemptSequence++;s.readStartedTick=s.readCompletedTick=GetTickCount64();tick(s);}center.started=GetTickCount64()-2201;s.readAttemptSequence++;s.readStartedTick=s.readCompletedTick=GetTickCount64();tick(s);});centerWorker.join();pump();check("center terminal crosses UI thread",delivered.size()==count+1&&delivered.back()["data"].value("ok",false)&&delivered.back()["data"]["sampleCount"]>=30&&!centerActive.load());
 count=delivered.size();std::thread savedWorker([]{applySavedNotification();});savedWorker.join();pump();check("Apply saved notification uses same UI lane",delivered.size()==count+1);
 bool pass=wrongApartment==0;for(const auto& c:checks)pass=pass&&c["pass"].get<bool>();json report={{"pass",pass},{"wrongApartmentCalls",wrongApartment},{"uiThread",uiThread},{"checks",checks},{"frames",originalEvents},{"allEvents",delivered},{"hardwareIo",false},{"settingsWrite",false},{"actualWindowsMessagePump",true}};std::ofstream(argv[1])<<report.dump(2);DestroyWindow(g_hwnd);std::cout<<report.dump()<<std::endl;return pass?0:1;}
