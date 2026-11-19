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
static void appendNativeLifecycleLog(const char* e,const json& d){logs.push_back({{"event",e},{"detail",d}});}
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
 if(text.rfind(L"--dev-unhide",0)==0)++unhideCalls;else if(text.rfind(L"--dev-hide",0)==0)++hideCalls;
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

static void physicalReleaseWindowCloseDutyPump(ULONGLONG) {}
static void physicalReleaseWindowClose(const char*) { throw std::runtime_error("unselected real Close branch"); }
static int physicalReleaseWindowGameBarTriState(bool* available) { *available=false; return -1; }

@@METHODS@@









static void reset(){
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
int main(){
 json cases=json::array();
 for(const auto* mode:{"pending-valid-current","pending-deadline-on-dispatch","pending-expired-before-pump","pending-stage0-cancel","pending-host-changed","pending-intent-changed","pending-power-edge"}) {
  reset();bool pass=false;std::string error;ULONGLONG originalIntent=g_inputHostVirtualIntentRevision.load(),originalHost=host;
  try {
   g_inputHostPrepared=false;
   physicalReleaseWindowToggleRequest("actual-deferred-oem");
   if(!queued.empty() || g_physicalReleaseWindowStage!=0 || g_physicalReleaseWindowPendingUntil!=1500)throw std::runtime_error("actual Toggle did not retain stage0 pending under original500ms");
   fixtureTick=1400;g_inputHostPrepared=true;
   if(std::string(mode)=="pending-stage0-cancel")physicalReleaseWindowCancelBorrow("explicit-intent-cancel");
   if(std::string(mode)=="pending-host-changed")++host;
   if(std::string(mode)=="pending-intent-changed")++g_inputHostVirtualIntentRevision;
   if(std::string(mode)=="pending-power-edge"){++power;physicalReleaseWindowOnPowerEdge(power);}
   if(std::string(mode)=="pending-expired-before-pump")fixtureTick=1600;
   physicalReleaseWindowPump();
   const auto queuedCount=queued.size();
   if(std::string(mode)=="pending-valid-current" || std::string(mode)=="pending-deadline-on-dispatch") {
    if(queuedCount!=1)throw std::runtime_error("current pending failed to queue once");
   }
   if(std::string(mode)=="pending-deadline-on-dispatch")fixtureTick=1500;
   while(!queued.empty()){auto work=std::move(queued.front());queued.erase(queued.begin());work();}
   if(std::string(mode)=="pending-valid-current") {
    if(unhideCalls!=1||cycleCalls!=1||g_physicalReleaseWindowStage!=1||!g_physicalReleaseWindowNeutral)throw std::runtime_error("current pending borrow lost normal behavior");
   } else if(unhideCalls||cycleCalls||assignCalls||g_physicalReleaseWindowNeutral || g_physicalReleaseWindowPendingUntil!=0)throw std::runtime_error("retired deferred intent acted or remained pending");
   pass=true;
  }catch(const std::exception& e){error=e.what();}
  cases.push_back({{"name",mode},{"pass",pass},{"error",error},{"originalIntent",originalIntent},{"currentIntent",g_inputHostVirtualIntentRevision.load()},
   {"originalHost",originalHost},{"currentHost",host},{"tick",fixtureTick},{"unhideCalls",unhideCalls},{"cycleCalls",cycleCalls},{"assignCalls",assignCalls},
   {"stage",g_physicalReleaseWindowStage.load()},{"neutral",g_physicalReleaseWindowNeutral.load()},{"pendingUntil",g_physicalReleaseWindowPendingUntil.load()},{"logs",logs}});
 }
 const bool ok=std::all_of(cases.begin(),cases.end(),[](const json& c){return c["pass"].get<bool>();});
 std::cout<<json{{"case","t1-pending-borrow"},{"pass",ok},{"cases",cases},{"hardwareIo",false},{"actualDeferredPump",true},
  {"fullUiOffOnReachability",false},{"fullConcurrencyProof",false}}.dump();return ok?0:1;
}
