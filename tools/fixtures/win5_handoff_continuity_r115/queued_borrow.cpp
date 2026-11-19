#include <algorithm>
#include <atomic>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
#include "input_host_diagnostic_dispatch.h"
using json=nlohmann::json;
using ULONGLONG=unsigned long long;using DWORD=unsigned long;
using InputHostMutex=std::mutex;
static ULONGLONG fixtureTick=1000,power=3,host=7;
static std::string persona="steamdeck",runId="fixture-host";
static std::atomic<ULONGLONG> g_inputHostVirtualIntentRevision{9};
static std::atomic<bool> g_exitRequested{false},g_inputReady{true};
static int unhideCalls=0,hideCalls=0,cycleCalls=0,assignCalls=0,supersedeCalls=0,initCalls=0;
static bool exitFence=false,personaTransition=false;
static std::function<void()> preOpenInterleave;
static std::vector<std::function<void()>> queued;
static json logs=json::array();
static bool observeDiagnosticScope=false,fixtureUnhideFailure=false;
static std::vector<bool> observedDiagnosticScopes;
static ULONGLONG GetTickCount64(){return fixtureTick;}
static ULONGLONG currentPowerGeneration(){return power;}
[[maybe_unused]] static ULONGLONG inputHostEpochToken(){return host;}
static bool inputHostExitFenceRaised(){return exitFence;}
static bool inputHostPersonaTransitionPending(){return personaTransition;}
static bool ymccSlotHandoffPlanBusy(){return false;}
static std::string inputHostPersona(){return persona;}
[[maybe_unused]] static std::string inputHostConfiguredPersona(){return persona;}
static void focusIsolationReadAuthoritativeHost(ULONGLONG* h,std::string* r){*h=host;*r=runId;}
[[maybe_unused]] static std::string W2U(const std::wstring& s){std::string out;for(auto c:s)out.push_back(static_cast<char>(c));return out;}
static std::wstring U2W(const std::string& s){return {s.begin(),s.end()};}
static void appendNativeLifecycleLog(const char* e,const json& d){if(observeDiagnosticScope)observedDiagnosticScopes.push_back(ymcc::inputHost::diagnostics::Scope::active());logs.push_back({{"event",e},{"detail",d}});}
static bool gamepadSerialSubmit(std::function<void()> work){queued.push_back(std::move(work));return true;}
static bool g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostActive=true;
static std::atomic<bool> g_inputHostPowerAdmissionRevoked{false},g_inputHostTupleHealPending{false};
static std::atomic<ULONGLONG> g_inputHostHealthySinceTick{100};
static ULONGLONG g_exitSessionId=1;
static struct {bool enabled=true,virtualTargetEnabled=true;} g_realStickTest;
enum class PowerLifecycle{Ready,Suspended};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::mutex g_inputHostMx,g_hidHidePhysicalIdentityMx;
static struct {std::vector<std::wstring> hiddenByUs{L"HID_FIXTURE_OWNED"};} g_hidHideVisibilityJournal;
struct HidHidePhysicalIdentity {std::string baseContainerDevicePath;};
static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
static std::atomic<bool> g_gamebarTraceAvailable{true},g_gamebarTraceVisible{false},g_gamebarTraceInputRedirected{false};
static std::atomic<ULONGLONG> g_gamebarTraceObservedTick{1000},g_gamebarTraceVersion{1},g_gamebarTraceInitAttempts{1};
static std::atomic<bool> g_physicalReleaseWindowDutyActive{false};
static std::vector<std::wstring> g_physicalReleaseWindowBorrowedRoots;
@@DECLARATIONS@@
static bool hidHideRun(const std::wstring& text,std::string*,DWORD*,ULONGLONG){
 if(text.rfind(L"--dev-unhide",0)==0){++unhideCalls;if(fixtureUnhideFailure)return false;}else if(text.rfind(L"--dev-hide",0)==0)++hideCalls;
 else throw std::runtime_error("unadmitted memory CLI operation");return true;
}
static json physicalReleaseWindowRunCycles(const std::vector<std::wstring>&,ULONGLONG,const char*,bool* ok,std::vector<std::wstring>* roots){++cycleCalls;*ok=true;roots->push_back(L"USB_FIXTURE_OWNED");return {{"cyclePlanned",1}};}
static bool ymccOpenXInputAvailable(){return false;}
static bool ymccOpenXInputAssignSlot(const std::wstring&,int,bool& ok){++assignCalls;ok=true;return true;}
static json physicalReleaseWindowGameBarProcDiag(){return json::object();}
static void gamebarTraceEnsureInitIfDue(){++initCalls;}
static void gamebarTraceRefresh(const char*){if(preOpenInterleave){auto f=std::move(preOpenInterleave);f();}}
static void physicalReleaseWindowDutyHandOffToT0(const char*){}
static void physicalReleaseWindowDutySupersedeByNewBorrow(){++supersedeCalls;}
static void physicalReleaseWindowRequest(const char*){throw std::runtime_error("unselected Close route");}
@@METHODS@@









static void reset(){
 observeDiagnosticScope=false;fixtureUnhideFailure=false;observedDiagnosticScopes.clear();
 fixtureTick=1000;power=3;host=7;persona="steamdeck";runId="fixture-host";g_inputHostVirtualIntentRevision=9;
 g_exitRequested=false;g_inputReady=true;exitFence=personaTransition=false;preOpenInterleave={};queued.clear();logs=json::array();
 unhideCalls=hideCalls=cycleCalls=assignCalls=supersedeCalls=initCalls=0;
 g_realStickTest={true,true};g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;
 g_inputHostTupleHealPending=false;g_inputHostHealthySinceTick=100;g_physicalReleaseWindowDutyActive=false;
 g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;g_hidHideVisibilityJournal.hiddenByUs={L"HID_FIXTURE_OWNED"};
 g_physicalReleaseWindowStage=0;g_physicalReleaseWindowTransition=0;g_physicalReleaseWindowTokenId=41;
 g_physicalReleaseWindowNeutral=false;g_physicalReleaseWindowCancelRequested=false;g_physicalReleaseWindowStaleByPower=false;
 g_physicalReleaseWindowPowerDeferred=false;g_physicalReleaseWindowDeferredCleanup=false;g_physicalReleaseWindowPendingUntil=0;
 g_physicalReleaseWindowLastToggle=0;g_physicalReleaseWindowDeferredRetryAt=0;g_physicalReleaseWindowPendingSource=nullptr;
 g_physicalReleaseWindowReleased.clear();g_physicalReleaseWindowBorrowedRoots.clear();g_physicalReleaseWindowDeadline=0;
 g_physicalReleaseWindowProbeAt=0;g_physicalReleaseWindowEvidenceDeadline=0;g_physicalReleaseWindowGameBarSeen=false;
}
static int runDiagnosticScopeCases(){
 json cases=json::array();
 for(const auto* mode:{"open-log-success","open-log-refused","open-log-expired","open-log-rollback"}) {
  reset();bool pass=false;std::string error;
  try {
   physicalReleaseWindowToggleRequest("scope-test-oem");
   if(queued.size()!=1)throw std::runtime_error("actual Toggle failed to queue");
   if(std::string(mode)=="open-log-refused")g_inputHostPrepared=false;
   if(std::string(mode)=="open-log-expired")fixtureTick+=kT1BorrowPendingValidityMs;
   if(std::string(mode)=="open-log-rollback")fixtureUnhideFailure=true;
   observeDiagnosticScope=true;
   auto work=std::move(queued.front());queued.clear();work();
   if(observedDiagnosticScopes.empty()||!std::all_of(observedDiagnosticScopes.begin(),observedDiagnosticScopes.end(),[](bool scoped){return scoped;}))throw std::runtime_error("actual Open emitted control log outside existing diagnostic Scope");
   if(ymcc::inputHost::diagnostics::Scope::active())throw std::runtime_error("Scope leaked past callback return");
   if(std::string(mode)=="open-log-success"){
    if(g_physicalReleaseWindowStage!=1||!g_physicalReleaseWindowNeutral||unhideCalls!=1||cycleCalls!=1)throw std::runtime_error("ordinary borrow changed");
   }else if(g_physicalReleaseWindowStage!=0||g_physicalReleaseWindowNeutral)throw std::runtime_error("ordinary refused/expired/rollback terminal changed");
   pass=true;
  }catch(const std::exception& e){error=e.what();}
  cases.push_back({{"name",mode},{"pass",pass},{"error",error},{"scopeObserved",observedDiagnosticScopes},{"scopeAfterReturn",ymcc::inputHost::diagnostics::Scope::active()},
   {"unhideCalls",unhideCalls},{"hideCalls",hideCalls},{"cycleCalls",cycleCalls},{"stage",g_physicalReleaseWindowStage.load()},{"neutral",g_physicalReleaseWindowNeutral.load()},{"logs",logs}});
 }
 const bool ok=std::all_of(cases.begin(),cases.end(),[](const json& c){return c["pass"].get<bool>();});
 std::cout<<json{{"case","t1-open-diagnostic-scope"},{"pass",ok},{"cases",cases},{"actualOpenAndRealScopeHeader",true},{"loggerPortOnlyObservesScope",true},
  {"actualDiskSinkBlocked",false},{"endToEndDispatcherTest",false},{"hardwareIo",false}}.dump();return ok?0:1;
}

int main(int argc,char** argv){
 if(argc==2&&std::string(argv[1])=="diagnostic-scope")return runDiagnosticScopeCases();
 if(argc!=1){std::cerr<<"Unknown queued fixture selection";return 2;}
 json cases=json::array();
 for(const auto* mode:{"cancel-before-dispatch","power-before-dispatch","host-before-dispatch","intent-before-dispatch","persona-before-dispatch","expired-before-dispatch","slow-pre-open-cancel","normal-current","new-current-after-cancel","second-cancel-before-dispatch"}){
  reset();bool pass=false;std::string error;
  try{
   physicalReleaseWindowToggleRequest("oem-fixture");
   if(queued.size()!=1||g_physicalReleaseWindowStage!=2)throw std::runtime_error("actual Toggle did not queue its Open");
   if(std::string(mode)=="cancel-before-dispatch"){physicalReleaseWindowCancelBorrow("virtual-target-disabled");++g_inputHostVirtualIntentRevision;}
   if(std::string(mode)=="power-before-dispatch"){++power;physicalReleaseWindowOnPowerEdge(power);}

   if(std::string(mode)=="host-before-dispatch")++host;
   if(std::string(mode)=="intent-before-dispatch")++g_inputHostVirtualIntentRevision;
   if(std::string(mode)=="persona-before-dispatch")persona="dualsense-edge";
   if(std::string(mode)=="expired-before-dispatch")fixtureTick+=kT1BorrowPendingValidityMs;
   if(std::string(mode)=="slow-pre-open-cancel")preOpenInterleave=[](){physicalReleaseWindowCancelBorrow("during-pre-open-observation");};
   if(std::string(mode)=="new-current-after-cancel"||std::string(mode)=="second-cancel-before-dispatch")physicalReleaseWindowCancelBorrow("retire-first-intent");
   auto work=std::move(queued.front());queued.erase(queued.begin());work();

   if(std::string(mode)=="new-current-after-cancel"||std::string(mode)=="second-cancel-before-dispatch"){
    if(unhideCalls||cycleCalls||g_physicalReleaseWindowStage!=0)throw std::runtime_error("cancelled opening did not return to idle");
    fixtureTick+=1300;physicalReleaseWindowToggleRequest("new-oem-intent");
    if(queued.size()!=1)throw std::runtime_error("new explicit intent could not queue");
    if(std::string(mode)=="second-cancel-before-dispatch")physicalReleaseWindowCancelBorrow("cancel-new-current-intent");
    auto next=std::move(queued.front());queued.clear();next();
   }
   if(std::string(mode)=="normal-current"||std::string(mode)=="new-current-after-cancel"){
    if(unhideCalls!=1||cycleCalls!=1||g_physicalReleaseWindowStage!=1||!g_physicalReleaseWindowNeutral)throw std::runtime_error("current ordinary borrow changed");
   }else if(unhideCalls||cycleCalls||assignCalls||g_physicalReleaseWindowNeutral)throw std::runtime_error("retired queued borrow acted after cancellation or power edge");
   pass=true;
  }catch(const std::exception& e){error=e.what();}
  cases.push_back({{"name",mode},{"pass",pass},{"error",error},{"unhideCalls",unhideCalls},{"hideCalls",hideCalls},
    {"cycleCalls",cycleCalls},{"assignCalls",assignCalls},{"stage",g_physicalReleaseWindowStage.load()},
    {"cancelRequested",g_physicalReleaseWindowCancelRequested.load()},{"staleByPower",g_physicalReleaseWindowStaleByPower.load()},
    {"neutral",g_physicalReleaseWindowNeutral.load()},{"logs",logs}});
 }
 const bool ok=std::all_of(cases.begin(),cases.end(),[](const json& c){return c["pass"].get<bool>();});
 std::cout<<json{{"case","t1-queued-borrow"},{"pass",ok},{"cases",cases},{"hardwareIo",false},{"actualQueueCallback",true}}.dump();
 return ok?0:1;
}
