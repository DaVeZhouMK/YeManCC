#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define YMCC_MODULE_MAINTENANCE_MUTEX_NAME L"Local\\YMCC.Updater.ModuleAdmission.Fixture"
#include <windows.h>
#include <atomic>
#include <string>
#include <deque>
#include <vector>
#include <functional>
#include <thread>
#include <chrono>
#include <iostream>
#include "json.hpp"
#include "module_maintenance_admission.h"
using json=nlohmann::json;
static int checks=0,physicalBoundary=0,payloadReads=0,replays=0;
static void need(bool value,const char* name){if(!value)throw std::runtime_error(name);++checks;}
namespace ymcc {enum class StartupInputState{Initializing,Waiting,Skipped,Ready,Failed};namespace controllerPersona{static bool currentOutput(const std::string& p){return p=="xbox360";}}}
static std::string fixturePersona="xbox360";
static bool fixturePayload=true,fixtureSubmit=true,fixtureCompleteSetup=false;
static std::vector<std::string> events;
static std::deque<std::function<void()>> mailbox;
static std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0,g_inputHostStartProtectionBoundTicket=0;
static std::uint64_t inputHostStartProtectionNow(){return GetTickCount64();}
static std::string inputHostConfiguredPersona(){return fixturePersona;}
static const char* inputHostMachineTargetRejectionReason(const std::string&){return nullptr;}
static std::atomic<bool> g_externalPriorityVirtualSuspended{false},g_inputModuleReplayPending{false},g_exitRequested{false},g_inputHostFaultHoldsStart{false},g_gamepadSetupReadQueued{false},g_inputHostSwitchExecutionReturnPending{false},g_inputHostSwitchRecoveryPending{false};
static std::atomic<ULONGLONG> g_moduleMaintenanceNextProbe{0};
static std::atomic<ymcc::StartupInputState> g_startupInputState{ymcc::StartupInputState::Waiting};
static bool g_startupInputAttempted=false,g_startupInputStartSucceeded=false;
static bool inputModulePayloadCurrent(){++payloadReads;return fixturePayload;}
static void appendNativeLifecycleLog(const char* name,const json&){events.push_back(name);}
static void publishStartupInputState(ymcc::StartupInputState state,const char*){g_startupInputState=state;}
static void inputHostStartProtectionFinish(std::uint64_t,const char*,const char*){}
static struct Setup {bool running=false;bool active()const{return running;}} g_gamepadDriverSetup;
static bool gamepadSerialSubmit(std::function<void()> work){if(!fixtureSubmit)return false;mailbox.push_back(std::move(work));return true;}
static bool gamepadDriverSetupPollAndPublish(){if(fixtureCompleteSetup)g_gamepadDriverSetup.running=false;return true;}
static void inputHostQueueLifecycleWork(const char*){++replays;}
//__PRODUCTION_START_PREFIX__
//__PRODUCTION_STARTUP_RESULT__
//__PRODUCTION_REPLAY_REQUEST__
static void drain(){while(!mailbox.empty()){auto item=std::move(mailbox.front());mailbox.pop_front();item();}}
static void reset(){fixturePersona="xbox360";fixturePayload=true;fixtureSubmit=true;fixtureCompleteSetup=false;g_gamepadDriverSetup.running=false;g_inputModuleReplayPending=false;g_exitRequested=false;g_inputHostFaultHoldsStart=false;g_gamepadSetupReadQueued=false;g_moduleMaintenanceNextProbe=0;g_startupInputState=ymcc::StartupInputState::Waiting;g_startupInputAttempted=false;g_startupInputStartSucceeded=false;physicalBoundary=payloadReads=replays=0;mailbox.clear();events.clear();}
struct ExternalOwner{
 HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::atomic<bool> held{false};std::thread worker;
 ExternalOwner(){worker=std::thread([this]{HANDLE gate=CreateMutexW(nullptr,FALSE,YMCC_MODULE_MAINTENANCE_MUTEX_NAME);if(gate){auto wait=WaitForSingleObject(gate,1000);held=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;}SetEvent(ready);WaitForSingleObject(release,5000);if(gate){if(held)ReleaseMutex(gate);CloseHandle(gate);}});need(WaitForSingleObject(ready,2000)==WAIT_OBJECT_0&&held,"fixture owner did not acquire real module mutex");}
 ~ExternalOwner(){SetEvent(release);worker.join();CloseHandle(ready);CloseHandle(release);}
};
int main(){try{
 reset();g_gamepadDriverSetup.running=true;
 need(!inputHostStart(),"regular startup raced active module preparation");need(g_inputModuleReplayPending&&physicalBoundary==0&&payloadReads==0,"prepare deferral reached source observer or wasted hash IO");
 recordStartupInputAttempt(false);need(g_startupInputState==ymcc::StartupInputState::Skipped&&!g_inputHostFaultHoldsStart,"transient preparation poisoned startup or render handshake");
 fixtureCompleteSetup=true;gamepadDriverSetupRequestRead();drain();need(replays==1&&!g_inputModuleReplayPending,"completion did not queue original lifecycle exactly once");
 need(inputHostStart()&&physicalBoundary==1,"target did not become eligible after preparation");gamepadDriverSetupRequestRead();drain();need(replays==1,"ready setup replayed multiple starts");
 reset();{ExternalOwner owner;auto started=std::chrono::steady_clock::now();need(!inputHostStart(),"manual module owner was bypassed");need(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(300),"module probe blocked native start");gamepadDriverSetupRequestRead();drain();need(replays==0&&payloadReads==0&&g_inputModuleReplayPending,"busy owner triggered hash/restart activity");gamepadDriverSetupRequestRead();need(mailbox.empty(),"deferred probe was not rate bounded");}
 g_moduleMaintenanceNextProbe=0;gamepadDriverSetupRequestRead();drain();need(replays==1&&!g_inputModuleReplayPending&&!g_inputHostFaultHoldsStart,"external owner release did not self heal");
 reset();fixturePayload=false;need(!inputHostStart(),"unverified module files were admitted");gamepadDriverSetupRequestRead();drain();need(g_inputHostFaultHoldsStart&&!g_inputModuleReplayPending&&replays==0,"terminal mismatch did not stop background reopen");auto readCount=payloadReads;gamepadDriverSetupRequestRead();drain();need(payloadReads==readCount,"terminal mismatch caused ongoing hash polling");
 reset();g_inputModuleReplayPending=true;g_exitRequested=true;gamepadDriverSetupRequestRead();need(mailbox.empty()&&replays==0,"exit intent still submitted module replay");
 reset();g_inputModuleReplayPending=true;fixtureSubmit=false;gamepadDriverSetupRequestRead();need(!g_gamepadSetupReadQueued,"failed serial submission permanently wedged read ownership");fixtureSubmit=true;g_moduleMaintenanceNextProbe=0;gamepadDriverSetupRequestRead();drain();need(replays==1,"failed queue could not recover");
 reset();g_inputModuleReplayPending=true;fixturePersona="disabled";gamepadDriverSetupRequestRead();drain();need(replays==1&&!inputHostStart()&&physicalBoundary==0,"deferred module completion replayed user-disabled output");
 need(ymcc::moduleMaintenance::transientPrepareRejection("prepare-rejected","usbip-version-maintenance-in-progress"),"busy verified prepare response is not deferred");
 need(!ymcc::moduleMaintenance::transientPrepareRejection("prepare-rejected","usbip-reboot-required"),"reboot fence was converted into an automatic restart storm");
 need(!ymcc::moduleMaintenance::transientPrepareRejection("protocol-error","usbip-version-maintenance-in-progress"),"unverified protocol response was accepted for automatic replay");
 std::cout<<"MODULE_STARTUP_INTERLOCK_PASS checks="<<checks<<" realMutex=true hardwareIo=false consumer=production-start-prefix/startup-result/serial-replay\n";return 0;
 }catch(const std::exception& error){std::cerr<<error.what()<<"\n";return 1;}}
