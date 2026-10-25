#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <deque>
#include <functional>
#include <filesystem>
#include <fstream>
#include <thread>
#include <future>
#include <chrono>
#include <memory>
#include <iostream>
#include "process_efficiency.h"
#include "gamepad_driver_setup_notice.h"
static std::function<void()> onWait;
static DWORD fakeWaitResult=WAIT_TIMEOUT;
static DWORD WINAPI controlledWait(HANDLE,DWORD){if(onWait)onWait();return fakeWaitResult;}
static BOOL WINAPI controlledExit(HANDLE,LPDWORD code){*code=0;return TRUE;}
#define WaitForSingleObject controlledWait
#define GetExitCodeProcess controlledExit
#define private public
#include "gamepad_driver_setup.h"
#undef private
#undef GetExitCodeProcess
#undef WaitForSingleObject
using json=nlohmann::json;
static std::atomic<bool> g_exitRequested{false};
static ymcc::GamepadDriverSetup* fixtureSetup=nullptr;
static std::deque<std::function<void()>> jobs;
static bool acceptJobs=true;
static unsigned polls=0;
static std::atomic<bool> g_inputModuleReplayPending{false};
static bool inputModulePayloadCurrent(){return true;}
namespace ymcc::moduleMaintenance { static bool available(){return true;} }
static std::atomic<bool> g_inputHostFaultHoldsStart{false};
static void appendNativeLifecycleLog(const char*,const json&){}
static bool inputHostQueueLifecycleWork(const char*){return true;}
static bool gamepadSerialSubmit(std::function<void()> work){if(!acceptJobs)return false;jobs.push_back(std::move(work));return true;}
static bool gamepadDriverSetupPollAndPublish(){++polls;return fixtureSetup->poll();}

static std::string W2U(const std::wstring& value){const auto bytes=std::filesystem::path(value).u8string();return std::string(bytes.begin(),bytes.end());}
static json helperReply;
static unsigned helperCalls=0,eventCalls=0;
struct RunOut {std::string out;DWORD exitCode=0;bool ran=true;};
static RunOut runCapture(const std::wstring& command,DWORD budget){
 if(command.find(L"--usbip-observe")==std::wstring::npos||command.find(L"--prepare")!=std::wstring::npos||budget!=5000)throw std::runtime_error("unadmitted helper command");
 ++helperCalls;return {helperReply.dump(),0,true};
}
static void ipc_emit(const char* event,const json&){if(std::string(event)!="gamepad.driverSetup")throw std::runtime_error("unexpected event");++eventCalls;}
static std::function<json(const json&)> observedHandler;
static void ipc_on(const char* command,std::function<json(const json&)> handler){if(std::string(command)!="gamepad.driverSetup.get")throw std::runtime_error("unexpected IPC");observedHandler=std::move(handler);}

#define g_gamepadDriverSetup (*fixtureSetup)
#include "usbip_snapshot_consumer.inc"
#include "usbip_setup_async_consumer.inc"
static void registerObservedHandler(){
#include "usbip_read_handler.inc"
}
#undef g_gamepadDriverSetup
static void writeStage(ymcc::GamepadDriverSetup& setup,const char* phase,unsigned sequence){
 auto rootBytes=std::filesystem::path(setup.root_).u8string();json doc={{"batch","GP-USBIP-UPGRADE-1"},{"ownerPid",GetCurrentProcessId()},{"installRoot",std::string(rootBytes.begin(),rootBytes.end())},{"session",setup.sessionToken_},{"usbipUpgrade",{{"schemaVersion",1},{"phase",phase},{"installedVersion","0.9.7.7"},{"targetVersion","0.9.8.1"},{"detail","controlled original consumer; no device IO"},{"sequence",sequence}}}};
 std::ofstream(std::filesystem::path(setup.report_+L".usbip.json"),std::ios::binary)<<doc.dump();
}
static void seed(ymcc::GamepadDriverSetup& setup,const std::filesystem::path& root){
 std::filesystem::create_directories(root);setup.root_=root.wstring();setup.report_=(root/L"report.json").wstring();setup.sessionToken_="robustness-session";
 setup.usbipUpgrade_={{"schemaVersion",1},{"phase","installing"},{"installedVersion","0.9.7.7"},{"targetVersion","0.9.8.1"},{"detail","seed"},{"sequence",1u}};setup.publishUsbipLocked();
 setup.state_.store(ymcc::GamepadDriverSetup::Pending);setup.process_=CreateEventW(nullptr,TRUE,FALSE,nullptr);setup.started_=GetTickCount64();
}
int wmain(int argc,wchar_t** argv){try{
 if(argc!=2)return 2;const auto out=std::filesystem::path(argv[1]);std::filesystem::create_directories(out);json checks=json::array();
 auto need=[&](const char* name,bool passed,json measurements=json::object()){checks.push_back({{"name",name},{"passed",passed},{"measurements",measurements}});if(!passed)throw std::runtime_error(name);};
 {ymcc::GamepadDriverSetup setup;seed(setup,out/L"exit-race");writeStage(setup,"installing",2);fakeWaitResult=WAIT_OBJECT_0;onWait=[&]{writeStage(setup,"complete",3);};setup.poll();onWait={};need("final stage between first read and observed exit is retained",setup.usbipUpgradeSnapshot().value("phase","")=="complete"&&!setup.active());need("completed reads remain idle",!setup.poll());}
 {ymcc::GamepadDriverSetup setup;seed(setup,out/L"cache-lock");std::promise<void> held;auto ready=held.get_future();std::thread owner([&]{std::lock_guard<std::mutex> lock(setup.mutex_);held.set_value();std::this_thread::sleep_for(std::chrono::milliseconds(180));});ready.wait();auto before=std::chrono::steady_clock::now();const auto state=setup.usbipUpgradeSnapshot();const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count();owner.join();need("cached UI snapshot never waits on the IO mutex",elapsed<50&&state.value("phase","")=="installing",{{"elapsedMs",elapsed},{"workerHeldMs",180}});}
 {ymcc::GamepadDriverSetup setup;seed(setup,out/L"async-request");fixtureSetup=&setup;fakeWaitResult=WAIT_TIMEOUT;onWait={};polls=0;jobs.clear();g_gamepadSetupReadQueued=false;std::promise<void> held;auto ready=held.get_future();std::thread owner([&]{std::lock_guard<std::mutex> lock(setup.mutex_);held.set_value();std::this_thread::sleep_for(std::chrono::milliseconds(180));});ready.wait();auto before=std::chrono::steady_clock::now();for(int i=0;i<200;++i)gamepadDriverSetupRequestRead();const auto elapsed=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-before).count();need("200 UI messages coalesce without reading disk or waiting for worker lock",elapsed<50&&jobs.size()==1&&polls==0,{{"elapsedMs",elapsed},{"queued",jobs.size()},{"polls",polls}});owner.join();auto work=std::move(jobs.front());jobs.pop_front();work();need("original worker performs exactly one sample",polls==1&&!g_gamepadSetupReadQueued.load());
 acceptJobs=false;gamepadDriverSetupRequestRead();need("rejected queue submission releases coalescing ownership",jobs.empty()&&!g_gamepadSetupReadQueued.load());acceptJobs=true;gamepadDriverSetupRequestRead();need("next normal event can request observation after queue rejection",jobs.size()==1);
 g_exitRequested=true;work=std::move(jobs.front());jobs.pop_front();work();need("late queued work after exit does not sample or revive owner",polls==1&&!g_gamepadSetupReadQueued.load());gamepadDriverSetupRequestRead();need("exit rejects all fresh observation work",jobs.empty());g_exitRequested=false;}

 {ymcc::GamepadDriverSetup setup;seed(setup,out/L"live-refresh");setup.state_.store(ymcc::GamepadDriverSetup::Failed);setup.usbipUpgrade_["phase"]="failed";setup.usbipUpgrade_["detail"]="Access to the path is denied";setup.publishUsbipLocked();
  int reads=0;auto read=[&](const std::wstring& command){++reads;need("refresh launches observation only with no prepare mode",command.find(L"--usbip-observe")!=std::wstring::npos&&command.find(L"--prepare")==std::wstring::npos);auto bytes=std::filesystem::path(setup.root_).u8string();return json{{"batch","GP-USBIP-OBSERVE-1"},{"ownerPid",GetCurrentProcessId()},{"installRoot",std::string(bytes.begin(),bytes.end())},{"session",setup.sessionToken_},{"usbipUpgrade",{{"schemaVersion",1},{"phase","current"},{"installedVersion","0.9.8.1"},{"targetVersion","0.9.8.1"},{"detail","current verified inventory"},{"sequence",0}}}};};
  need("completed failed startup accepts current read-only inventory",setup.refreshUsbipObservation(read));need("live inventory clears stale presentation without rewriting installer state",setup.usbipUpgradeSnapshot().value("phase","")=="current"&&setup.usbipUpgradeSnapshot().value("observationOnly",false)&&setup.state()==ymcc::GamepadDriverSetup::Failed);
  need("startup error preserved separately as history",setup.usbipMaintenanceSnapshot().value("phase","")=="failed"&&setup.usbipMaintenanceSnapshot().value("detail","")=="Access to the path is denied");for(int i=0;i<200;++i)setup.refreshUsbipObservation(read);need("200 repeated status requests share one observation",reads==1);
  setup.lastObservationAt_=0;need("unchanged current observation produces no repeated event",!setup.refreshUsbipObservation(read)&&reads==2);need("repeat live observation does not replace failure history",setup.usbipMaintenanceSnapshot().value("phase","")=="failed");
  setup.lastObservationAt_=0;need("invalid session cannot replace current observation",!setup.refreshUsbipObservation([&](const std::wstring& cmd){auto value=read(cmd);value["session"]="old-session";return value;})&&setup.usbipUpgradeSnapshot().value("phase","")=="current");
  setup.lastObservationAt_=0;setup.usbipUpgrade_["phase"]="reboot-required";setup.publishUsbipLocked();need("live version cannot clear current-session mandatory reboot",!setup.refreshUsbipObservation(read)&&setup.usbipUpgradeSnapshot().value("phase","")=="reboot-required");
  setup.lastObservationAt_=0;setup.state_.store(ymcc::GamepadDriverSetup::Pending);int before=reads;need("active installer never runs a concurrent status helper",!setup.refreshUsbipObservation(read)&&reads==before);
 }

 {ymcc::GamepadDriverSetup setup;seed(setup,out/L"actual-ipc-handler");fixtureSetup=&setup;setup.state_.store(ymcc::GamepadDriverSetup::Failed);setup.usbipUpgrade_["phase"]="failed";setup.publishUsbipLocked();registerObservedHandler();
  auto bytes=std::filesystem::path(setup.root_).u8string();helperReply={{"batch","GP-USBIP-OBSERVE-1"},{"ownerPid",GetCurrentProcessId()},{"installRoot",std::string(bytes.begin(),bytes.end())},{"session",setup.sessionToken_},{"usbipUpgrade",{{"schemaVersion",1},{"phase","current"},{"installedVersion","0.9.8.1"},{"targetVersion","0.9.8.1"},{"detail","actual readonly handler fixture"},{"sequence",0}}}};
  helperCalls=eventCalls=0;need("actual cached status get never creates a helper",observedHandler(json::object()).at("usbipUpgrade").value("phase","")=="failed"&&helperCalls==0);
  const auto refreshed=observedHandler({{"refreshUsbip",true}});need("actual refresh IPC consumes live inventory and broadcasts it",refreshed.at("usbipUpgrade").value("phase","")=="current"&&helperCalls==1&&eventCalls==1);
  need("actual refresh snapshot preserves startup failure separately",refreshed.at("usbipMaintenanceHistory").value("phase","")=="failed"&&refreshed.at("driverSetup")==ymcc::GamepadDriverSetup::Failed);
  observedHandler({{"refreshUsbip",true}});need("actual repeated IPC does not repeat helper or broadcast",helperCalls==1&&eventCalls==1);
  g_exitRequested=true;setup.lastObservationAt_=0;observedHandler({{"refreshUsbip",true}});need("actual IPC exit fence rejects new observation",helperCalls==1&&eventCalls==1);g_exitRequested=false;
 }
 std::ofstream(out/L"usbip-robustness-native.json")<<json({{"status","PASS"},{"checks",checks},{"actualConsumers",{"GamepadDriverSetup.poll","GamepadDriverSetup.usbipUpgradeSnapshot","gamepadDriverSetupRequestRead extracted from current main.cpp"}},{"hardwareIo",false},{"nativeWindowLoopExecuted",false}}).dump(2);std::cout<<"USBIP_ROBUSTNESS_NATIVE_PASS checks="<<checks.size()<<" hardwareIo=false\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}}
