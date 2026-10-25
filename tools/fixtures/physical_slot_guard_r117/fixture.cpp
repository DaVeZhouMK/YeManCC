#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;using DWORD=unsigned long;using BYTE=unsigned char;using ULONGLONG=unsigned long long;using USHORT=unsigned short;using USAGE=unsigned short;
/*ACTUAL_TYPES*/
static std::mutex g_hidHidePhysicalIdentityMx;static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
static ULONGLONG fakeNow=10000,g_openXInputSlotGuardLastTick=0;
static ULONGLONG GetTickCount64(){return fakeNow;}
static std::wstring U2W(const std::string&s){return {s.begin(),s.end()};}static std::string W2U(const std::wstring&s){std::string r;for(const auto c:s)r.push_back(static_cast<char>(c));return r;}
static DWORD g_xinputPrimarySlot=0,g_xinputConnectedMask=2,g_xinputVirtualEndpointMask=0,g_xinputSlotUnknownMask=0;
static bool g_xinputPhysicalSourceLocked=true,available=true,exitFence=false,releaseWindow=false;
static std::atomic<bool>g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputCaptureStop{false},g_externalPriorityVirtualSuspended{false},g_physicalReleaseWindowDutyActive{false};
enum class PowerLifecycle{Ready,Suspended};static std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long>g_powerGeneration{3},g_xinputTopologyEpoch{2},g_inputHostVirtualIntentRevision{4},g_inputHostEpochMirror{7};static std::atomic<unsigned>g_inputHostVirtualIntentWriters{0};
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static bool inputHostExitFenceRaised(){return exitFence;}static bool physicalReleaseWindowActive(){return releaseWindow;}
static bool ymccOpenXInputAvailable(){return available;}static int virtualGuardCalls=0,queries=0,writes=0;static BYTE probeSlot=1,assignedSlot=0xff;static bool fixtureAssigned=true,acceptedResult=true;
static std::function<void()>duringQuery,duringAssign;static json logs=json::array();
static void ymccOpenXInputVirtualSlotGuard(){++virtualGuardCalls;}
static BYTE ymccOpenXInputGetUserIndex(const std::wstring&){++queries;const auto observed=probeSlot;if(duringQuery){auto hook=std::move(duringQuery);duringQuery={};hook();}return observed;}
using OpenXInputAssignGuard=bool(*)(void*) noexcept;
static bool ymccOpenXInputAssignSlot(const std::wstring&,BYTE slot,bool&accepted,OpenXInputAssignGuard guard=nullptr,void*context=nullptr){if(guard&&!guard(context)){accepted=false;return false;}++writes;assignedSlot=slot;accepted=acceptedResult;if(duringAssign)duringAssign();return fixtureAssigned;}
static void appendNativeLifecycleLog(const char*event,const json&body){auto row=body;row["event"]=event;logs.push_back(row);}
/*ACTUAL_HELPERS*/
/*ACTUAL_GUARD*/
static json cases=json::array(),checks=json::array();static void check(const char*name,bool pass){checks.push_back({{"name",name},{"pass",pass}});}
static void reset(){fakeNow=10000;g_openXInputSlotGuardLastTick=0;g_xinputPrimarySlot=0;g_xinputConnectedMask=2;g_xinputVirtualEndpointMask=g_xinputSlotUnknownMask=0;g_xinputPhysicalSourceLocked=available=true;exitFence=releaseWindow=false;g_inputReady=true;g_inputHostPowerAdmissionRevoked=g_inputCaptureStop=g_externalPriorityVirtualSuspended=g_physicalReleaseWindowDutyActive=false;g_powerLifecycle=PowerLifecycle::Ready;g_powerGeneration=3;g_xinputTopologyEpoch=2;g_inputHostVirtualIntentRevision=4;g_inputHostVirtualIntentWriters=0;g_inputHostEpochMirror=7;g_realStickTest={};virtualGuardCalls=queries=writes=0;probeSlot=1;assignedSlot=0xff;fixtureAssigned=acceptedResult=true;duringQuery={};duringAssign={};logs=json::array();g_hidHidePhysicalIdentity={};auto&p=g_hidHidePhysicalIdentity;p.resolved=true;p.physicalIdentity="PHYSICAL-A";p.sourcePath="HID-A-PATH";p.deviceInstanceId="HID-A";p.baseContainerDevicePath="SAVED-A";p.baseContainerDeviceInstanceId="ROOT-A";}
static void finish(const std::string&name){bool pass=std::all_of(checks.begin(),checks.end(),[](const json&c){return c["pass"].get<bool>();});cases.push_back({{"name",name},{"pass",pass},{"checks",checks},{"queries",queries},{"writes",writes},{"assignedSlot",assignedSlot},{"logs",logs}});checks=json::array();}
int main(){try{
 (void)physicalReleaseWindowActive();(void)inputHostExitFenceRaised();(void)hidHideIsVirtualIdentity(W2U(L""),"");
 for(const bool virt:{false,true}){reset();g_realStickTest.virtualTargetEnabled=virt;ymccOpenXInputSlotGuard();check("healthy physical drift assigns once to the original empty slot",queries==1&&writes==1&&assignedSlot==0);check("virtual guard retains its original single call",virtualGuardCalls==1);finish(virt?"healthy-virtual-enabled":"healthy-native-off");}
 reset();probeSlot=0;g_xinputConnectedMask=1;ymccOpenXInputSlotGuard();check("already-correct physical slot no write",queries==1&&writes==0);finish("healthy-stable");
 reset();ymccOpenXInputSlotGuard();ymccOpenXInputSlotGuard();fakeNow+=999;ymccOpenXInputSlotGuard();check("existing one-second cadence preserved",queries==1&&writes==1);fakeNow++;ymccOpenXInputSlotGuard();check("next original interval checks once",queries==2&&writes==2);finish("existing-cadence");
 const std::vector<std::string> modes={"exit","not-ready","suspend","revoked","capture-stop","priority","release-window","close-duty","disabled","source-unresolved","source-route-lost","source-root-lost","source-replaced","source-path-replaced","source-identity-replaced","source-virtual","primary-changed","source-unlocked","power-generation","topology-generation","host-generation","intent-revision","intent-writer","target-connected","target-virtual","target-unknown","probe-unknown"};
 auto mutate=[&](const std::string&m){if(m=="exit")exitFence=true;else if(m=="not-ready")g_inputReady=false;else if(m=="suspend")g_powerLifecycle=PowerLifecycle::Suspended;else if(m=="revoked")g_inputHostPowerAdmissionRevoked=true;else if(m=="capture-stop")g_inputCaptureStop=true;else if(m=="priority")g_externalPriorityVirtualSuspended=true;else if(m=="release-window")releaseWindow=true;else if(m=="close-duty")g_physicalReleaseWindowDutyActive=true;else if(m=="disabled")g_realStickTest.enabled=false;else if(m=="source-unresolved")g_hidHidePhysicalIdentity.resolved=false;else if(m=="source-route-lost")g_hidHidePhysicalIdentity.baseContainerDevicePath.clear();else if(m=="source-root-lost")g_hidHidePhysicalIdentity.baseContainerDeviceInstanceId.clear();else if(m=="source-replaced")g_hidHidePhysicalIdentity.deviceInstanceId="HID-B";else if(m=="source-path-replaced")g_hidHidePhysicalIdentity.sourcePath="HID-B-PATH";else if(m=="source-identity-replaced")g_hidHidePhysicalIdentity.physicalIdentity="PHYSICAL-B";else if(m=="source-virtual")g_hidHidePhysicalIdentity.baseContainerDevicePath="ROOT\\HIDMAESTRO\\VIRTUAL";else if(m=="primary-changed")g_xinputPrimarySlot=2;else if(m=="source-unlocked")g_xinputPhysicalSourceLocked=false;else if(m=="power-generation")++g_powerGeneration;else if(m=="topology-generation")++g_xinputTopologyEpoch;else if(m=="host-generation")++g_inputHostEpochMirror;else if(m=="intent-revision")++g_inputHostVirtualIntentRevision;else if(m=="intent-writer")g_inputHostVirtualIntentWriters=1;else if(m=="target-connected")g_xinputConnectedMask|=1;else if(m=="target-virtual")g_xinputVirtualEndpointMask|=1;else if(m=="target-unknown")g_xinputSlotUnknownMask|=1;else if(m=="probe-unknown")g_xinputSlotUnknownMask|=2;};
 for(const auto&mode:modes){reset();duringQuery=[&,mode]{mutate(mode);};ymccOpenXInputSlotGuard();check("query invalidation cannot issue a physical remap",writes==0);finish("query/"+mode);}
 for(const auto&mode:{"exit","not-ready","suspend","revoked","capture-stop","priority","release-window","close-duty","disabled","source-unresolved","source-route-lost","source-root-lost","source-virtual","source-unlocked","intent-writer","target-connected","target-virtual","target-unknown","probe-unknown"}){reset();mutate(mode);ymccOpenXInputSlotGuard();check("closed initial physical admission no write",writes==0);finish(std::string("initial/")+mode);}
 reset();probeSlot=0xff;ymccOpenXInputSlotGuard();check("unknown route no assignment",writes==0&&queries==1);finish("mapping-unavailable");
 reset();available=false;ymccOpenXInputSlotGuard();check("absent proxy does no work",virtualGuardCalls==0&&queries==0&&writes==0);finish("proxy-unavailable");
 reset();g_xinputPrimarySlot=4;ymccOpenXInputSlotGuard();check("invalid desired slot no work",queries==0&&writes==0);finish("invalid-target-slot");
 unsigned failed=0,assertions=0,failedChecks=0;for(const auto&c:cases){if(!c["pass"].get<bool>())++failed;for(const auto&k:c["checks"]){++assertions;if(!k["pass"].get<bool>())++failedChecks;}}std::cout<<json({{"passed",failed==0},{"caseCount",cases.size()},{"assertions",assertions},{"failedCases",failed},{"failedChecks",failedChecks},{"cases",cases},{"deviceOperations",0},{"productExecuted",false}}).dump(2)<<"\n";return failed?1:0;
 }catch(const std::exception&e){std::cerr<<e.what()<<"\n";return 2;}}
