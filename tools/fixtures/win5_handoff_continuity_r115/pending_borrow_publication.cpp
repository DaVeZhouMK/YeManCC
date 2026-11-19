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
static std::function<void()> pendingContextInterleave;
static std::vector<std::function<void()>> queued;
static json logs=json::array();
static ULONGLONG GetTickCount64(){return fixtureTick;}
static ULONGLONG currentPowerGeneration(){return power;}
[[maybe_unused]] static ULONGLONG inputHostEpochToken(){return host;}
static bool inputHostExitFenceRaised(){return exitFence;}
static bool inputHostPersonaTransitionPending(){return personaTransition;}
static bool ymccSlotHandoffPlanBusy(){return false;}
static std::string inputHostPersona(){return persona;}
[[maybe_unused]] static std::string inputHostConfiguredPersona(){if(pendingContextInterleave){auto work=std::move(pendingContextInterleave);work();}return persona;}
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
 g_exitRequested=false;g_inputReady=true;exitFence=personaTransition=false;preOpenInterleave={};pendingContextInterleave={};queued.clear();logs=json::array();
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
 for(const auto* mode:{"pending-cancel-during-context-read","pending-new-owner-during-context-read"}) {
  reset();bool pass=false;std::string error;
  try {
   g_inputHostPrepared=false;physicalReleaseWindowToggleRequest("old-pending");
   if(g_physicalReleaseWindowPendingUntil!=1500 || !queued.empty())throw std::runtime_error("old pending not established");
   fixtureTick=1400;g_inputHostPrepared=true;
   const bool replace=std::string(mode)=="pending-new-owner-during-context-read";
   pendingContextInterleave=[replace] {
    physicalReleaseWindowCancelBorrow("cancel-original-during-current-read");
    if(replace){++g_inputHostVirtualIntentRevision;fixtureTick=2700;g_inputHostPrepared=false;
     physicalReleaseWindowToggleRequest("new-current-pending");g_inputHostPrepared=true;}
   };
   physicalReleaseWindowPump();
   if(!queued.empty() || unhideCalls || cycleCalls || g_physicalReleaseWindowStage!=0)throw std::runtime_error("old pending dispatched across publication gap");
   if(replace) {
    if(g_physicalReleaseWindowPendingUntil!=3200)throw std::runtime_error("old Pump cleared the newer pending");
    fixtureTick=2800;physicalReleaseWindowPump();
    if(queued.size()!=1)throw std::runtime_error("newer valid pending could not dispatch");
    auto work=std::move(queued.front());queued.clear();work();
    if(unhideCalls!=1||cycleCalls!=1||g_physicalReleaseWindowStage!=1||!g_physicalReleaseWindowNeutral)throw std::runtime_error("new owner lost its normal borrow");
   }else if(g_physicalReleaseWindowPendingUntil!=0 || g_physicalReleaseWindowNeutral)throw std::runtime_error("cancelled pending remained live");
   pass=true;
  }catch(const std::exception& e){error=e.what();}
  cases.push_back({{"name",mode},{"pass",pass},{"error",error},{"unhideCalls",unhideCalls},{"cycleCalls",cycleCalls},
   {"stage",g_physicalReleaseWindowStage.load()},{"neutral",g_physicalReleaseWindowNeutral.load()},{"pendingUntil",g_physicalReleaseWindowPendingUntil.load()},{"logs",logs}});
 }
 const bool ok=std::all_of(cases.begin(),cases.end(),[](const json& c){return c["pass"].get<bool>();});
 std::cout<<json{{"case","t1-pending-borrow-publication"},{"pass",ok},{"cases",cases},{"actualDeferredPump",true},
  {"controlledCurrentStateReadInterleaving",true},{"realThreadScheduling",false},{"hardwareIo",false},{"fullConcurrencyProof",false}}.dump();return ok?0:1;
}
