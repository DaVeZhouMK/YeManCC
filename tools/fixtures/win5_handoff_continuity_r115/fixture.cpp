// R115: only process-local deterministic stand-ins for OS/driver/clock observations.
// The production masks, classifier, budgets, queue, fence, gates, schedule and entire
// handoff step are injected byte-for-byte by the adjacent Node runner.
#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
using json=nlohmann::json;
using DWORD=unsigned long;
using BYTE=unsigned char;
using ULONGLONG=unsigned long long;
using InputHostMutex=std::mutex;
static ULONGLONG fakeNow=10000;
static ULONGLONG GetTickCount64(){return fakeNow;}
static std::wstring U2W(const std::string& s){return {s.begin(),s.end()};}
static std::string W2U(const std::wstring& s){std::string out;for(wchar_t c:s){if(c>127)throw std::runtime_error("fixture accepts ASCII identities only");out.push_back(static_cast<char>(c));}return out;}
struct HidHidePhysicalIdentity {
 bool resolved=true;
 std::string physicalIdentity="fixture-source",sourcePath="fixture-path",deviceInstanceId="fixture-instance";
 std::string baseContainerDevicePath="physical-A";
 std::string baseContainerDeviceInstanceId="USB\\VID_045E&PID_028E\\7&2EA17D0F&0&1";
};
static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
static std::mutex g_hidHidePhysicalIdentityMx;
static struct {HidHidePhysicalIdentity physical;}g_hidHideVisibilityJournal;
static unsigned long long g_inputHostEpoch=8;
static std::atomic<unsigned long long>g_inputHostEpochMirror{8},g_inputHostVirtualIntentRevision{0};static std::atomic<unsigned>g_inputHostVirtualIntentWriters{0};
static std::string g_inputHostRunId="r115-inert-run";
static std::string persona="elite",g_exitSessionId="r115-inert-exit";
static const char* inputHostPersona(){return persona.c_str();}
static std::string inputHostConfiguredPersona(){return persona;}
static std::atomic<bool>g_externalPriorityVirtualSuspended{false};
static std::atomic<unsigned long long>g_powerGeneration{3},g_xinputTopologyEpoch{2};
static DWORD g_xinputConnectedMask=3,g_xinputVirtualEndpointMask=2,g_xinputSlotUnknownMask=0,g_xinputPrimarySlot=0;
static bool g_xinputPhysicalSourceLocked=true;
static struct {bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
enum class PowerLifecycle {Ready,Suspended};
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputCaptureStop{false};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_physicalReleaseWindowDutyActive{false};
static bool physicalReleaseWindowActive(){return false;}
static constexpr ULONGLONG kPhysicalReleaseWindowDutyPollMs=250;
static std::atomic<bool> g_inputHostExitFence{false};
static std::atomic<unsigned long long>g_inputHostExitFenceRefusals{0},g_inputHostExitFenceRefusalRows{0};
static constexpr unsigned long long kInputHostExitFenceRefusalRowMax=8;
static std::mutex g_inputHostLifecycleWorkMx;
static std::deque<std::string>g_inputHostLifecycleWorkQueue;
static std::atomic<bool>g_inputHostLifecycleWorkRequested{false},g_inputHostLifecycleWorkInFlight{false};
static InputHostMutex g_inputHostMx;
static json events=json::array(),io=json::array();
static void appendNativeLifecycleLog(const char* event,const json& body){auto row=body;row["event"]=event;row["tick"]=fakeNow;events.push_back(row);}
static bool msiNativeRearIntentCurrent(const std::string&){return false;}
static void inputHostSwitchAuditEnqueue(const std::string&,size_t){}
static int virtualIndex=1,physicalIndex=0,unknownIndex=-1,assignCalls=0,cycleCalls=0,reanchorCalls=0;
static bool available=true,endpointVisible=true,assignResult=true,assignAccepted=true,rollbackResult=true,cycleResult=false;
static std::wstring virtualPath=L"\\\\?\\hid#hidmaestro_vid_045e_pid_0b00&ig_00#r115-virtual";
static std::function<void()>afterAssign,afterCycle,afterVirtualIndex,beforeAssignIssue;
static bool skipAssignIssue=false;
static bool ymccOpenXInputAvailable(){return available;}
static std::wstring ymccOpenXInputGetDevicePath(DWORD slot){
 if(endpointVisible&&virtualIndex==int(slot))return virtualPath;
 if(physicalIndex==int(slot))return L"physical-A";
 // Simulates the existing sampled Unknown bit: connected, but path query failed.
 return {};
}
static BYTE ymccOpenXInputGetUserIndex(const std::wstring& p){
 if(p==virtualPath){const auto observed=BYTE(virtualIndex);if(afterVirtualIndex){auto hook=std::move(afterVirtualIndex);afterVirtualIndex={};hook();}return observed;}
 if(p==L"physical-A")return BYTE(physicalIndex);
 return 0xFF;
}
using OpenXInputAssignGuard=bool(*)(void*) noexcept;
static bool ymccOpenXInputAssignSlot(const std::wstring& p,BYTE slot,bool& accepted,OpenXInputAssignGuard guard=nullptr,void*context=nullptr){
 if(skipAssignIssue){accepted=false;return false;}
 if(beforeAssignIssue){auto hook=std::move(beforeAssignIssue);beforeAssignIssue={};hook();}
 if(guard&&!guard(context)){accepted=false;return false;}
 ++assignCalls;
 io.push_back({{"op","assign"},{"path",W2U(p)},{"slot",slot},{"exitFence",g_inputHostExitFence.load()},{"tick",fakeNow}});
 if(p==virtualPath){accepted=assignAccepted;if(assignResult&&accepted){virtualIndex=slot;if(physicalIndex==slot)physicalIndex=-1;if(unknownIndex==slot)unknownIndex=-1;}if(afterAssign)afterAssign();return assignResult;}
 accepted=rollbackResult;if(rollbackResult){physicalIndex=slot;if(virtualIndex==slot)virtualIndex=1;}return rollbackResult;
}
static void hidHideCycleClearBusyIfStale(const char*){}
static bool hidHideCycleInstance(const std::wstring& root,const char* phase){
 ++cycleCalls;io.push_back({{"op","cycle"},{"root",W2U(root)},{"phase",phase},{"exitFence",g_inputHostExitFence.load()}});
 if(cycleResult)physicalIndex=1;if(afterCycle)afterCycle();return cycleResult;
}
static void ymccOpenXInputReanchorPhysicalSource(){++reanchorCalls;}
@@CLASSIFIER@@
@@MASKS@@
@@FENCE@@
@@QUEUE@@
@@RAISE@@
@@VISIBLE@@
@@RESOLVE@@
@@REFRESH@@
@@BUDGET@@
@@PLAN@@
static int g_openXInputVirtualSlotAttempts=0;
static constexpr int kOpenXInputVirtualSlotMaxAttempts=4;
@@GUARD@@
static ULONGLONG g_openXInputSlotGuardLastTick=0;
@@SLOTGUARD@@
static void fixtureSuppressedDispatch(const std::string& reason){
@@SUSPENDED_DISPATCH@@
}
static json cases=json::array(),checks=json::array(),precondition=nullptr;
static void expect(const char* label,const json& actual,const json& expected){checks.push_back({{"label",label},{"actual",actual},{"expected",expected},{"passed",actual==expected}});}
static json state(){return {{"queue",g_inputHostLifecycleWorkQueue},{"requested",g_inputHostLifecycleWorkRequested.load()},{"inFlight",g_inputHostLifecycleWorkInFlight.load()},
 {"active",g_ymccSlotHandoffPlan.active},{"activeMirror",g_ymccSlotHandoffPlanActive.load()},{"busy",ymccSlotHandoffPlanBusy()},{"stage",g_ymccSlotHandoffPlan.stage},
 {"nextDue",g_ymccSlotHandoffPlan.nextDueTick},{"deadline",g_ymccSlotHandoffPlan.stageDeadline},{"operations",ymccSlotIntentOperations()},{"budgetKey",g_ymccSlotIntent.key},
 {"liveRoute",g_hidHidePhysicalIdentity.baseContainerDevicePath},{"exhausted",g_ymccSlotIntent.exhausted},{"confirmed",g_ymccSlotIntent.confirmedSlot0},
 {"connectedMask",g_xinputConnectedMask},{"virtualMask",g_xinputVirtualEndpointMask},{"unknownMask",g_xinputSlotUnknownMask},
 {"physicalMask",xinputPhysicalConnectedMask()},{"nonVirtualMask",xinputNonVirtualConnectedMask()},{"assignCalls",assignCalls},{"cycleCalls",cycleCalls},
 {"virtualIndex",virtualIndex},{"physicalIndex",physicalIndex},{"unknownIndex",unknownIndex},{"io",io}};}
static void finish(const char* name,const char* category="control"){
 bool passed=true;for(const auto& c:checks)passed=passed&&c["passed"].get<bool>();
 cases.push_back({{"name",name},{"category",category},{"passed",passed},{"checks",checks},{"precondition",precondition},{"state",state()},{"events",events}});checks=json::array();
}
static void reset(){
 skipAssignIssue=false;beforeAssignIssue={};
 g_externalPriorityVirtualSuspended=false;g_powerGeneration=3;g_xinputTopologyEpoch=2;afterVirtualIndex={};
 fakeNow=10000;virtualPath=L"\\\\?\\hid#hidmaestro_vid_045e_pid_0b00&ig_00#r115-virtual";g_hidHidePhysicalIdentity=HidHidePhysicalIdentity{};g_hidHideVisibilityJournal.physical=g_hidHidePhysicalIdentity;
 g_inputHostEpochMirror=8;g_inputHostVirtualIntentRevision=0;g_inputHostVirtualIntentWriters=0;g_inputHostEpoch=8;g_inputHostRunId="r115-inert-run";persona="elite";g_xinputConnectedMask=3;g_xinputVirtualEndpointMask=2;g_xinputSlotUnknownMask=0;g_xinputPrimarySlot=0;
 g_realStickTest={};g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputCaptureStop=false;g_powerLifecycle=PowerLifecycle::Ready;
 g_physicalReleaseWindowDutyActive=false;g_inputHostExitFence=false;g_inputHostExitFenceRefusals=0;g_inputHostExitFenceRefusalRows=0;
 g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;g_inputHostLifecycleWorkInFlight=false;
 g_ymccSlotIntent={};g_ymccSlotHandoffPlan={};g_ymccSlotHandoffPlanActive=false;g_openXInputVirtualSlotAttempts=0;g_openXInputSlotGuardLastTick=0;precondition=nullptr;
 virtualIndex=1;physicalIndex=0;unknownIndex=-1;assignCalls=cycleCalls=reanchorCalls=0;
 available=endpointVisible=assignResult=assignAccepted=rollbackResult=true;cycleResult=false;afterAssign={};afterCycle={};events=json::array();io=json::array();
}
// Mechanical dequeue driver only: no model of production gate/state-machine logic.
static void step(bool advance=true){
 if(g_inputHostLifecycleWorkQueue.empty())throw std::runtime_error("missing production queue token");
 const auto reason=g_inputHostLifecycleWorkQueue.front();g_inputHostLifecycleWorkQueue.pop_front();
 g_inputHostLifecycleWorkRequested=!g_inputHostLifecycleWorkQueue.empty();
 if(reason!=kYmccSlotStepReason)throw std::runtime_error("unexpected reason");
 if(advance)fakeNow=std::max(fakeNow,g_ymccSlotHandoffPlan.nextDueTick);
 g_inputHostLifecycleWorkInFlight=true;ymccOpenXInputSlotHandoffStep();g_inputHostLifecycleWorkInFlight=false;
}
static void toOperation(){expect("schedule",ymccSlotHandoffSchedule(),true);step();expect("actual observation selected stage 4",g_ymccSlotHandoffPlan.stage,4);}
int main(int argc,char** argv){
 const std::string filter=argc>1?argv[1]:"";
 const auto run=[&](const char* name,const std::function<void()>& f){if(filter.empty()||filter==name){reset();f();}};
 run("red-unknown-occupant-guard",[]{
  g_xinputSlotUnknownMask=1;physicalIndex=-1;unknownIndex=0;precondition=state();
  ymccOpenXInputSlotGuard();
  expect("Unknown connected occupant must prevent safe-window assignment",assignCalls,0);
  expect("blocked Unknown does not consume budget",ymccSlotIntentOperations(),0);
  finish("red-unknown-occupant-guard","independent-contract");
 });
 run("red-unknown-live-key-retains-old-admission",[]{
  g_xinputConnectedMask=2;physicalIndex=-1;assignResult=false;ymccOpenXInputSlotGuard();
  expect("first actual failed attempt consumes one operation",ymccSlotIntentOperations(),1);
  const int callsBefore=assignCalls;assignResult=true;fakeNow+=1000;
  g_hidHidePhysicalIdentity.baseContainerDevicePath.clear();precondition=state();
  ymccOpenXInputSlotGuard();
  expect("missing live key cannot authorize assignment using remembered key",assignCalls-callsBefore,0);
  expect("unknown identity preserves one operation without spending another",ymccSlotIntentOperations(),1);
  finish("red-unknown-live-key-retains-old-admission","independent-contract");
 });
 run("red-exit-prune-orphans-pending-plan",[]{
  expect("production schedule queues plan",ymccSlotHandoffSchedule(),true);
  precondition=state();inputHostRaiseExitFence("r115-inert");
  expect("exit retains the already queued serial cancellation token",g_inputHostLifecycleWorkQueue.size(),1);
  expect("active plan must retain a serial cancellation owner",!ymccSlotHandoffPlanBusy()||g_inputHostLifecycleWorkInFlight.load()||!g_inputHostLifecycleWorkQueue.empty(),true);
  expect("new handoff business remains fence-rejected",inputHostQueueLifecycleWork(kYmccSlotStepReason),false);
  if(!g_inputHostLifecycleWorkQueue.empty()&&g_inputHostLifecycleWorkQueue.front()==kYmccSlotStepReason)step(false);
  expect("serial cancellation clears plan and mirror",ymccSlotHandoffPlanBusy(),false);
  expect("no slot side effects on exit",assignCalls+cycleCalls,0);
  finish("red-exit-prune-orphans-pending-plan","independent-contract");
 });
 run("exit-mixed-queue-retains-only-cleanup-and-cancellation",[]{
  ymccSlotHandoffSchedule();inputHostQueueLifecycleWork("persona-transition");inputHostQueueLifecycleWork("virtual-target-disabled");inputHostRaiseExitFence("mixed");
  expect("preserves cancellation plus original cleanup",g_inputHostLifecycleWorkQueue.size(),2);
  if(!g_inputHostLifecycleWorkQueue.empty()&&g_inputHostLifecycleWorkQueue.front()==kYmccSlotStepReason)step(false);
  expect("original cleanup remains queued",g_inputHostLifecycleWorkQueue.size()==1&&g_inputHostLifecycleWorkQueue.front()=="virtual-target-disabled",true);
  expect("slot plan no longer busy",ymccSlotHandoffPlanBusy(),false);expect("no slot writes",assignCalls+cycleCalls,0);finish("exit-mixed-queue-retains-only-cleanup-and-cancellation");
 });
 run("exit-cancellation-precedes-active-close-duty",[]{
  ymccSlotHandoffSchedule();g_physicalReleaseWindowDutyActive=true;inputHostRaiseExitFence("duty");
  if(!g_inputHostLifecycleWorkQueue.empty()&&g_inputHostLifecycleWorkQueue.front()==kYmccSlotStepReason)step(false);
  expect("duty cannot postpone memory cancellation",ymccSlotHandoffPlanBusy(),false);expect("no requeue",g_inputHostLifecycleWorkQueue.size(),0);expect("no writes or budget",assignCalls+cycleCalls+ymccSlotIntentOperations(),0);finish("exit-cancellation-precedes-active-close-duty");
 });
 run("priority-consumed-step-retires-memory-plan",[]{
  ymccSlotHandoffSchedule();const auto reason=g_inputHostLifecycleWorkQueue.front();g_inputHostLifecycleWorkQueue.pop_front();g_inputHostLifecycleWorkRequested=false;g_inputHostLifecycleWorkInFlight=true;g_externalPriorityVirtualSuspended=true;
  fixtureSuppressedDispatch(reason);expect("consuming suppressed step retires plan",ymccSlotHandoffPlanBusy(),false);expect("worker clears inflight",g_inputHostLifecycleWorkInFlight.load(),false);expect("no writes",assignCalls+cycleCalls,0);finish("priority-consumed-step-retires-memory-plan");
 });
 for(int mode=0;mode<12;++mode){
  const char* names[]={"query-source-lost-no-write","query-host-epoch-changed-no-write","query-persona-changed-no-write","query-exit-fence-no-write","query-virtual-off-no-write","query-power-revoked-no-write","query-unknown-arrival-no-write","query-power-generation-no-write","query-topology-generation-no-write","query-suspended-no-write","query-disabled-no-write","query-not-input-ready-no-write"};
  run(names[mode],[&,mode]{g_xinputConnectedMask=2;physicalIndex=-1;
   afterVirtualIndex=[&]{if(mode==0)g_hidHidePhysicalIdentity.baseContainerDevicePath.clear();if(mode==1)++g_inputHostEpoch;if(mode==2)persona="steamdeck";if(mode==3)inputHostRaiseExitFence("query-race");if(mode==4)g_realStickTest.virtualTargetEnabled=false;if(mode==5)g_inputHostPowerAdmissionRevoked=true;if(mode==6){g_xinputConnectedMask|=1;g_xinputSlotUnknownMask=1;unknownIndex=0;}if(mode==7)++g_powerGeneration;if(mode==8)++g_xinputTopologyEpoch;if(mode==9)g_powerLifecycle=PowerLifecycle::Suspended;if(mode==10)g_realStickTest.enabled=false;if(mode==11)g_inputReady=false;};
   ymccOpenXInputSlotGuard();expect("query invalidation prevents assignment",assignCalls,0);expect("query invalidation preserves operation budget",ymccSlotIntentOperations(),0);finish(names[mode],"query-boundary-contract");});
 }
 for(int mode=0;mode<6;++mode){const char* names[]={"guard-postassign-exit-no-false-health","guard-postassign-off-no-false-health","guard-postassign-source-no-false-health","guard-postassign-persona-no-false-health","guard-postassign-power-no-false-health","guard-postassign-topology-no-false-health"};run(names[mode],[&,mode]{g_xinputConnectedMask=2;physicalIndex=-1;afterAssign=[&]{if(mode==0)inputHostRaiseExitFence("guard-assign");if(mode==1)g_realStickTest.virtualTargetEnabled=false;if(mode==2)g_hidHidePhysicalIdentity.baseContainerDevicePath.clear();if(mode==3)persona="steamdeck";if(mode==4)++g_powerGeneration;if(mode==5)++g_xinputTopologyEpoch;};ymccOpenXInputSlotGuard();expect("one already admitted write only",assignCalls,1);expect("retired result not declared healthy",g_ymccSlotIntent.confirmedSlot0,false);expect("operation budget retained",ymccSlotIntentOperations(),1);finish(names[mode],"completion-context-contract");});}
 for(const bool handoff:{false,true}){
  run(handoff?"R118-handoff-no-proxy-no-operation-budget":"R118-guard-no-proxy-no-operation-budget",[&,handoff]{
   if(handoff)toOperation();else{g_xinputConnectedMask=2;physicalIndex=-1;}
   skipAssignIssue=true;if(handoff)step();else ymccOpenXInputVirtualSlotGuard();
   expect("not issued spends no operation",ymccSlotIntentOperations(),0);expect("no proxy writes",assignCalls,0);expect("not falsely healthy",g_ymccSlotIntent.confirmedSlot0,false);
   if(handoff)expect("same current request retains serial retry",ymccSlotHandoffPlanBusy()&&!g_inputHostLifecycleWorkQueue.empty(),true);else expect("guard write attempt count unchanged",g_openXInputVirtualSlotAttempts,0);
   finish(handoff?"R118-handoff-no-proxy-no-operation-budget":"R118-guard-no-proxy-no-operation-budget","issue-budget-contract");
  });
  for(int mode=0;mode<8;++mode){const char*names[]={"exit","off","capture-stop","priority","power","topology","intent-writer","intent-revision"};const auto name=std::string("R118-")+(handoff?"handoff-":"guard-")+names[mode];run(name.c_str(),[&,handoff,mode,name]{
   if(handoff)toOperation();else{g_xinputConnectedMask=2;physicalIndex=-1;}
   beforeAssignIssue=[&]{if(mode==0)inputHostRaiseExitFence("issue");if(mode==1)g_realStickTest.virtualTargetEnabled=false;if(mode==2)g_inputCaptureStop=true;if(mode==3)g_externalPriorityVirtualSuspended=true;if(mode==4)++g_powerGeneration;if(mode==5)++g_xinputTopologyEpoch;if(mode==6)g_inputHostVirtualIntentWriters=1;if(mode==7)++g_inputHostVirtualIntentRevision;};
   if(handoff)step();else ymccOpenXInputVirtualSlotGuard();expect("issue rejection spends no budget",ymccSlotIntentOperations(),0);expect("cancelled request issues no assignment",assignCalls,0);expect("cancelled request never confirmed",g_ymccSlotIntent.confirmedSlot0,false);finish(name.c_str(),"issue-admission-contract");
  });}
 }
 run("R118-noissue-transient-keeps-automatic-retry",[]{toOperation();skipAssignIssue=true;step();expect("unissued retained",ymccSlotHandoffPlanBusy(),true);expect("zero before retry",ymccSlotIntentOperations(),0);skipAssignIssue=false;cycleResult=true;g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId="USB\\VID_0B05&PID_1B4C\\inert-A";step();expect("automatic retry actually writes",assignCalls,1);expect("one operation total",ymccSlotIntentOperations(),1);expect("retry readback healthy",g_ymccSlotIntent.confirmedSlot0,true);finish("R118-noissue-transient-keeps-automatic-retry","issue-budget-contract");});
 run("R118-noissue-wait-is-fixed-and-finite",[]{toOperation();skipAssignIssue=true;const auto started=fakeNow;for(int n=0;n<200&&ymccSlotHandoffPlanBusy();++n)step();expect("unissued wait bounded by original segment length",fakeNow-started,4000);expect("wait ended without false operation exhaustion",!ymccSlotHandoffPlanBusy()&&!g_ymccSlotIntent.exhausted,true);expect("no writes or operation budget",assignCalls+ymccSlotIntentOperations(),0);finish("R118-noissue-wait-is-fixed-and-finite","issue-budget-contract");});
 run("known-physical-blocks-guard",[]{ymccOpenXInputSlotGuard();expect("no assignment",assignCalls,0);expect("no budget consumed",ymccSlotIntentOperations(),0);finish("known-physical-blocks-guard");});
 run("empty-physical-window-guard-confirms",[]{g_xinputConnectedMask=2;physicalIndex=-1;ymccOpenXInputSlotGuard();expect("assign once",assignCalls,1);expect("readback confirms",g_ymccSlotIntent.confirmedSlot0,true);finish("empty-physical-window-guard-confirms");});
 for(int mode=0;mode<5;mode++){
  const char* names[]={"guard-exit-stops","guard-off-stops","guard-power-stops","guard-persona-stops","guard-revoked-stops"};
  run(names[mode],[&,mode]{g_xinputConnectedMask=2;physicalIndex=-1;if(mode==0)inputHostRaiseExitFence("control");if(mode==1)g_realStickTest.virtualTargetEnabled=false;if(mode==2)g_powerLifecycle=PowerLifecycle::Suspended;if(mode==3)persona="ps5";if(mode==4)g_inputHostPowerAdmissionRevoked=true;ymccOpenXInputSlotGuard();expect("no assignment",assignCalls,0);finish(names[mode]);});
 }
 run("unknown-live-key-schedule-refuses-budget-preserved",[]{ymccSlotIntentSync();ymccSlotIntentReserveOperation();const auto key=g_ymccSlotIntent.key;g_hidHidePhysicalIdentity.baseContainerDevicePath.clear();expect("schedule refuses unknown live key",ymccSlotHandoffSchedule(),false);expect("retains operations",ymccSlotIntentOperations(),1);expect("retains prior key",g_ymccSlotIntent.key,key);finish("unknown-live-key-schedule-refuses-budget-preserved");});
 run("dequeued-step-exit-cancels",[]{
  ymccSlotHandoffSchedule();g_inputHostLifecycleWorkQueue.pop_front();g_inputHostLifecycleWorkRequested=false;g_inputHostLifecycleWorkInFlight=true;
  inputHostRaiseExitFence("control");ymccOpenXInputSlotHandoffStep();g_inputHostLifecycleWorkInFlight=false;
  expect("actual step clears plan and mirror",ymccSlotHandoffPlanBusy(),false);expect("zero side effects",assignCalls+cycleCalls,0);finish("dequeued-step-exit-cancels");
 });
 run("duty-wait-exit-requeue-refusal-clears",[]{ymccSlotHandoffSchedule();g_inputHostLifecycleWorkQueue.pop_front();g_inputHostLifecycleWorkRequested=false;g_physicalReleaseWindowDutyActive=true;inputHostRaiseExitFence("control");ymccOpenXInputSlotHandoffStep();expect("queue refusal clears plan",ymccSlotHandoffPlanBusy(),false);expect("no writes",assignCalls+cycleCalls,0);finish("duty-wait-exit-requeue-refusal-clears");});
 run("close-duty-wait-fixed-observation-deadline",[]{ymccSlotHandoffSchedule();const auto deadline=g_ymccSlotHandoffPlan.stageDeadline;g_physicalReleaseWindowDutyActive=true;for(int i=0;i<4;i++)step();expect("fixed deadline",g_ymccSlotHandoffPlan.stageDeadline,deadline);expect("budget unchanged",ymccSlotIntentOperations(),0);expect("one retained queue token",g_inputHostLifecycleWorkQueue.size(),1);expect("zero writes",assignCalls+cycleCalls,0);g_physicalReleaseWindowDutyActive=false;step();expect("progresses after duty settles",g_ymccSlotHandoffPlan.stage,4);finish("close-duty-wait-fixed-observation-deadline");});
 run("Win5-policy-skip-rollback-two-attempts",[]{
  ymccSlotHandoffSchedule();int n=0;while(g_ymccSlotHandoffPlan.active&&n++<100)step();
  expect("known skip unchanged",cycleCalls,0);expect("two assign and two rollback",assignCalls,4);expect("shared operation budget",ymccSlotIntentOperations(),2);expect("bounded exhaustion",g_ymccSlotIntent.exhausted,true);expect("no same-key rearm",ymccSlotHandoffSchedule(),false);expect("no active plan",ymccSlotHandoffPlanBusy(),false);finish("Win5-policy-skip-rollback-two-attempts");
 });
 run("cooldown-does-not-execute-early",[]{toOperation();step();const auto calls=assignCalls;step(false);expect("before retry deadline no writes",assignCalls,calls);expect("budget remains one",ymccSlotIntentOperations(),1);finish("cooldown-does-not-execute-early");});
 run("endpoint-absent-two-segments-9500ms",[]{endpointVisible=false;g_xinputVirtualEndpointMask=0;g_xinputConnectedMask=1;ymccSlotHandoffSchedule();int n=0;while(g_ymccSlotHandoffPlan.active&&n++<110)step();expect("two fixed observation segments and one cooldown",fakeNow-10000,9500);expect("read-only exhaustion",g_ymccSlotIntent.exhausted,true);expect("observations do not spend operations",ymccSlotIntentOperations(),0);expect("zero writes",assignCalls+cycleCalls,0);finish("endpoint-absent-two-segments-9500ms");});
 run("source-change-before-operation-cancels",[]{toOperation();g_hidHidePhysicalIdentity.baseContainerDevicePath="physical-B";step();expect("plan cleared",ymccSlotHandoffPlanBusy(),false);expect("zero writes",assignCalls+cycleCalls,0);finish("source-change-before-operation-cancels");});
 run("target-change-before-operation-cancels",[]{toOperation();virtualPath+=L"-replacement";step();expect("plan cleared",ymccSlotHandoffPlanBusy(),false);expect("zero writes",assignCalls+cycleCalls,0);finish("target-change-before-operation-cancels");});
 run("post-assign-exit-does-not-publish-success",[]{toOperation();afterAssign=[]{inputHostRaiseExitFence("during-inert-assign");};step();expect("only admitted assignment",assignCalls,1);expect("no extra cycle",cycleCalls,0);expect("not falsely confirmed",g_ymccSlotIntent.confirmedSlot0,false);expect("plan cleared",ymccSlotHandoffPlanBusy(),false);finish("post-assign-exit-does-not-publish-success");});
 run("nonpolicy-cycle-success-actual-target-readback",[]{g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId="USB\\VID_0B05&PID_1B4C\\inert-A";cycleResult=true;toOperation();step();expect("cycle called",cycleCalls,1);expect("no rollback",assignCalls,1);expect("readback confirmed",g_ymccSlotIntent.confirmedSlot0,true);expect("cleared",ymccSlotHandoffPlanBusy(),false);finish("nonpolicy-cycle-success-actual-target-readback");});
 run("nonpolicy-cycle-failure-frozen-source-rollback",[]{g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId="USB\\VID_0B05&PID_1B4C\\inert-A";toOperation();step();expect("cycle called",cycleCalls,1);expect("rollback original source path",io.back()["path"],"physical-A");expect("rollback original source slot",io.back()["slot"],0);expect("budget one",ymccSlotIntentOperations(),1);finish("nonpolicy-cycle-failure-frozen-source-rollback");});
 run("real-new-epoch-resets-budget",[]{ymccSlotIntentSync();ymccSlotIntentReserveOperation();ymccSlotIntentReserveOperation();ymccSlotIntentExhaust("fixture-exhaust");++g_inputHostEpoch;ymccSlotIntentSync();expect("new epoch resets operations",ymccSlotIntentOperations(),0);expect("new epoch unexhausted",g_ymccSlotIntent.exhausted,false);finish("real-new-epoch-resets-budget");});
 run("cycle-helper-policy-and-missing-root",[]{YmccSlotPhysicalRefreshOutcome r;expect("Win5 skip",ymccOpenXInputCycleDisplacedPhysical("inert",&r),false);expect("explicit reason",r.reason,"xusb-045e-policy");g_hidHideVisibilityJournal.physical.baseContainerDeviceInstanceId.clear();expect("missing root",ymccOpenXInputCycleDisplacedPhysical("inert",&r),false);expect("missing reason",r.reason,"physical-root-missing");expect("zero cycle requests",cycleCalls,0);finish("cycle-helper-policy-and-missing-root");});
 if(cases.empty()){std::cerr<<"unknown case\n";return 2;}
 int failures=0,checkCount=0,failedChecks=0;for(const auto& c:cases){failures+=!c["passed"].get<bool>();for(const auto& k:c["checks"]){++checkCount;failedChecks+=!k["passed"].get<bool>();}}
 std::cout<<json{{"cases",cases},{"caseCount",cases.size()},{"failedCases",failures},{"checkCount",checkCount},{"failedChecks",failedChecks},{"deviceIOExecuted",false}}.dump(2)<<std::endl;
 return failures?1:0;
}
