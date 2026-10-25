// Inert case-4 harness. Production helper, budget and case body are inserted verbatim.
#include "json.hpp"
#include <iostream>
#include <mutex>
#include <atomic>
#include <algorithm>
#include <string>
#include <vector>
using json=nlohmann::json;using DWORD=unsigned;using BYTE=unsigned char;using ULONGLONG=unsigned long long;using InputHostMutex=std::mutex;
static ULONGLONG fakeNow=10000,g_inputHostEpoch=8;
static ULONGLONG GetTickCount64(){return fakeNow;}static InputHostMutex g_inputHostMx;
struct {struct {std::string baseContainerDeviceInstanceId;}physical;}g_hidHideVisibilityJournal;
static std::wstring U2W(const std::string& s){return {s.begin(),s.end()};}
static std::string W2U(const std::wstring& s){std::string out;for(const auto c:s)out.push_back(static_cast<char>(c));return out;}
static DWORD g_xinputVirtualEndpointMask=2;
// Pre-admitted side effects are inert here; full live-gate invalidation is tested by R118 integrated caller fixture.
static std::string g_inputHostRunId="fixture-run";
static std::atomic<unsigned long long>g_powerGeneration{3},g_xinputTopologyEpoch{2},g_inputHostVirtualIntentRevision{0};static std::atomic<unsigned>g_inputHostVirtualIntentWriters{0};
static std::atomic<bool>g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputCaptureStop{false},g_externalPriorityVirtualSuspended{false},g_physicalReleaseWindowDutyActive{false};
enum class PowerLifecycle{Ready,Suspended};static std::atomic<PowerLifecycle>g_powerLifecycle{PowerLifecycle::Ready};static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static bool inputHostExitFenceRaised(){return false;}static bool physicalReleaseWindowActive(){return false;}static const char*inputHostPersona(){return "elite";}
static int cycleCalls=0,clearBusyCalls=0,assignCalls=0,rollbackCalls=0,virtualIndex=1,physicalIndex=0,readbacks=0,clearCalls=0,requeueCalls=0;
static bool cycleResult=true,assignResult=true,assignAccepted=true,rollbackResult=true,rollbackAccepted=true;
static std::string cyclePhase,cycleRoot;static json events=json::array();
static void appendNativeLifecycleLog(const char* event,const json& body){auto row=body;row["event"]=event;events.push_back(row);}
static void hidHideCycleClearBusyIfStale(const char*){clearBusyCalls++;}
static bool hidHideCycleInstance(const std::wstring& root,const char* phase){cycleCalls++;cycleRoot=W2U(root);cyclePhase=phase;if(cycleResult)physicalIndex=1;return cycleResult;}
@@REFRESH@@
// Source identity is pre-admitted in this case-4 diagnostic fixture.
// Full current-key/source invalidation is exercised by win5_handoff_continuity_r115_selftest.
static bool ymccSlotIntentKeyLocked(std::string& out){out="fixture-admitted-source";return true;}
@@BUDGET@@
struct {bool active=true,issueWaitLogged=false;unsigned long long epoch=8;std::string runId="fixture-run",intentKey="fixture-admitted-source",persona="elite";ULONGLONG issueAdmissionDeadline=0;DWORD targetSlot=1;std::wstring targetPath=L"virtual",frozenSourcePath=L"physical";DWORD frozenSourceSlot=0;int stage=4;ULONGLONG nextDueTick=0;}g_ymccSlotHandoffPlan;
static const char* ymccSlotHandoffOperationBlocker(bool){return nullptr;}
static DWORD xinputPhysicalConnectedMask(){return physicalIndex>=0&&physicalIndex<4?1u<<physicalIndex:0;}
using OpenXInputAssignGuard=bool(*)(void*) noexcept;
static bool ymccOpenXInputAssignSlot(const std::wstring& path,BYTE slot,bool& accepted,OpenXInputAssignGuard guard=nullptr,void*context=nullptr){
 if(guard&&!guard(context)){accepted=false;return false;}
 assignCalls++;
 if(path==L"virtual"){accepted=assignAccepted;if(assignResult&&accepted){virtualIndex=slot;physicalIndex=-1;}return assignResult;}
 rollbackCalls++;accepted=rollbackAccepted;if(rollbackResult&&accepted){physicalIndex=slot;virtualIndex=1;}return rollbackResult;
}
static DWORD ymccOpenXInputGetUserIndex(const std::wstring&){readbacks++;return static_cast<DWORD>(virtualIndex);}
static void ymccSlotHandoffPlanClear(const char*,bool){clearCalls++;}
static void ymccSlotHandoffQueueLog(const char*,const char*,ULONGLONG){}
static void ymccSlotHandoffPlanRequeue(){requeueCalls++;}
static void operation(){const ULONGLONG now=fakeNow;
@@CASE4@@
}
static json cases=json::array(),checks=json::array();
static void expect(const char* name,const json& actual,const json& expected,bool observation=false){checks.push_back({{"name",name},{"actual",actual},{"expected",expected},{"passed",actual==expected},{"observation",observation}});}
static void reset(const std::string& base){g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId=base;g_xinputVirtualEndpointMask=2;cycleCalls=clearBusyCalls=assignCalls=rollbackCalls=readbacks=clearCalls=requeueCalls=0;virtualIndex=1;physicalIndex=0;cycleResult=assignResult=assignAccepted=rollbackResult=rollbackAccepted=true;cyclePhase.clear();cycleRoot.clear();events=json::array();g_ymccSlotIntent=YmccSlotIntentBudget{};g_ymccSlotIntent.key="fixture-admitted-source";g_ymccSlotHandoffPlan={};fakeNow=10000;}
static json legacy(){json oldEvents=events;for(auto& e:oldEvents){e.erase("physicalRefreshOutcome");e.erase("physicalRefreshReason");e.erase("physicalRefreshCycleRequested");}return {{"cycleCalls",cycleCalls},{"clearBusyCalls",clearBusyCalls},{"assignCalls",assignCalls},{"rollbackCalls",rollbackCalls},{"readbacks",readbacks},{"clearCalls",clearCalls},{"requeueCalls",requeueCalls},{"operations",g_ymccSlotIntent.operations},{"exhausted",g_ymccSlotIntent.exhausted},{"confirmedSlot0",g_ymccSlotIntent.confirmedSlot0},{"confirmedPath",g_ymccSlotIntent.confirmedPath},{"virtualIndex",virtualIndex},{"physicalIndex",physicalIndex},{"endpointMask",g_xinputVirtualEndpointMask},{"stage",g_ymccSlotHandoffPlan.stage},{"nextDue",g_ymccSlotHandoffPlan.nextDueTick},{"cycleRoot",cycleRoot},{"cyclePhase",cyclePhase},{"events",oldEvents}};}
static void finish(const std::string& name){bool pass=true;for(const auto& check:checks)pass=pass&&check["passed"].get<bool>();cases.push_back({{"name",name},{"passed",pass},{"checks",checks},{"legacyBehavior",legacy()},{"events",events}});checks=json::array();}
static void helperCase(const std::string& name,const std::string& root,bool result,bool withOutcome,const char* phase,const char* outcome,const char* reason,int expectedCalls){
 reset(root);cycleResult=result;bool ok=false;json refresh=json::object();
 if(withOutcome){
#if YMCC_SLOT_REFRESH_OUTCOME
  YmccSlotPhysicalRefreshOutcome value{"stale","stale",true};ok=ymccOpenXInputCycleDisplacedPhysical(phase,&value);refresh={{"outcome",value.outcome},{"reason",value.reason},{"requested",value.cycleRequested}};
#else
  ok=ymccOpenXInputCycleDisplacedPhysical(phase);
#endif
 }else ok=ymccOpenXInputCycleDisplacedPhysical(phase);
 expect("legacy bool unchanged",ok,result&&expectedCalls!=0);expect("cycle calls",cycleCalls,expectedCalls);expect("busy clear calls",clearBusyCalls,expectedCalls);
 if(expectedCalls){expect("same root passed to existing call",cycleRoot,root);expect("phase and null fallback preserved",cyclePhase,phase?phase:"slot-handoff");}
 if(withOutcome){expect("refresh outcome",refresh.value("outcome","<absent>"),outcome,true);expect("refresh reason",refresh.value("reason","<absent>"),reason,true);expect("cycle request marker",refresh.value("requested",false),expectedCalls!=0,true);}
 finish(name);
}
static void stageCase(const std::string& name,const std::string& root,bool cycle,bool alreadyZero,bool assigned,bool accepted,bool rollback,int expectedCycles,int expectedRollbacks,bool exhausted,const char* outcome,const char* reason){
 reset(root);cycleResult=cycle;assignResult=assigned;assignAccepted=accepted;rollbackResult=rollback;
 if(alreadyZero){virtualIndex=0;physicalIndex=1;g_xinputVirtualEndpointMask=1;}
 operation();if(!alreadyZero&&!g_ymccSlotIntent.confirmedSlot0){fakeNow+=1500;operation();}
 const auto beforeExtra=legacy();if(g_ymccSlotIntent.exhausted)operation();
 expect("existing cycle calls",cycleCalls,expectedCycles);expect("existing rollback calls",rollbackCalls,expectedRollbacks);expect("exhaustion unchanged",g_ymccSlotIntent.exhausted,exhausted);
 if(exhausted){expect("two operation budget retained",g_ymccSlotIntent.operations,2);expect("exhaustion adds no device call",assignCalls,beforeExtra["assignCalls"]);}
 int handoffs=0,exhaustLogs=0;
 for(const auto& e:events){if(e["event"]=="gamepad-slot-handoff-exhausted"){exhaustLogs++;expect("existing exhaustion reason",e["reason"],"no-slot0-after-bounded-operations");}
  if(e["event"]!="gamepad-slot-handoff")continue;handoffs++;
  expect("logged legacy cycle bool",e["cycleOk"],expectedCycles!=0&&cycle);
  expect("logged rollback need unchanged",e["rollbackNeeded"],assigned&&accepted&&!(expectedCycles!=0&&cycle));
  expect("logged rollback verification unchanged",e["rollbackVerified"],expectedRollbacks!=0&&rollback);
  expect("low-frequency refresh outcome",e.value("physicalRefreshOutcome","<absent>"),outcome,true);
  expect("low-frequency refresh reason",e.value("physicalRefreshReason","<absent>"),reason,true);
  expect("low-frequency cycle request marker",e.value("physicalRefreshCycleRequested",false),expectedCycles!=0,true);
 }
 expect("one existing handoff event per operation",handoffs,alreadyZero?0:g_ymccSlotIntent.operations);expect("existing exhaustion event count",exhaustLogs,exhausted?1:0);
 if(alreadyZero){expect("already slot0 performs no assign",assignCalls,0);expect("already slot0 adds no event",events.size(),0);}
 if(!alreadyZero&&!exhausted){expect("only existing readback closes",g_ymccSlotIntent.confirmedSlot0,true);expect("readback uses target",readbacks,1);}
 finish(name);
}
int main(){const std::string win5="USB\\VID_045E&PID_028E\\7&2EA17D0F&0&1",other="USB\\VID_0B05&PID_1B4C\\inert-control";
 for(const bool withOutcome:{false,true}){const std::string suffix=withOutcome?"-optional-out":"-legacy-bool";
  helperCase("045E-policy"+suffix,win5,true,withOutcome,"slot-handoff","skipped","xusb-045e-policy",0);
  helperCase("missing-root"+suffix,"",true,withOutcome,"slot-handoff","unavailable","physical-root-missing",0);
  helperCase("cycle-success"+suffix,other,true,withOutcome,"custom-phase","succeeded","cycle-call-returned-true",1);
  helperCase("cycle-failure"+suffix,other,false,withOutcome,"custom-phase","failed","cycle-call-returned-false",1);
 }
 helperCase("null-phase-preserved",other,true,true,nullptr,"succeeded","cycle-call-returned-true",1);
 stageCase("Win5-101449-skip-rollback-exhaustion",win5,true,false,true,true,true,0,2,true,"skipped","xusb-045e-policy");
 stageCase("Win5-101700-skip-rollback-exhaustion",win5,true,false,true,true,true,0,2,true,"skipped","xusb-045e-policy");
 stageCase("normal-cycle-and-slot0-readback",other,true,false,true,true,true,1,0,false,"succeeded","cycle-call-returned-true");
 stageCase("cycle-call-false-retains-rollback",other,false,false,true,true,true,2,2,true,"failed","cycle-call-returned-false");
 stageCase("missing-root-still-rolls-back","",true,false,true,true,true,0,2,true,"unavailable","physical-root-missing");
 stageCase("already-slot0-no-refresh-or-new-event",win5,true,true,true,true,true,0,0,false,"not-needed","already-slot0");
 stageCase("assignment-failed-refresh-not-needed",other,true,false,false,false,true,0,0,true,"not-needed","assignment-failed");
 stageCase("assignment-unaccepted-refresh-not-needed",other,true,false,true,false,true,0,0,true,"not-needed","assignment-not-accepted");
 stageCase("policy-skip-unverified-rollback-slot0-readback-retained",win5,true,false,true,true,false,0,1,false,"skipped","xusb-045e-policy");
 int failed=0,total=0,failedChecks=0;for(const auto& c:cases){if(!c["passed"].get<bool>())failed++;for(const auto& check:c["checks"]){total++;failedChecks+=!check["passed"].get<bool>();}}
 std::cout<<json{{"cases",cases},{"caseCount",cases.size()},{"checks",total},{"failedCases",failed},{"failedChecks",failedChecks}}.dump(2)<<std::endl;return failed?1:0;
}
