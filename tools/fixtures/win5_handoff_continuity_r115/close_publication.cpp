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
using ULONGLONG=unsigned long long;using DWORD=unsigned long;using BYTE=unsigned char;
static constexpr DWORD ERROR_SUCCESS=0;
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
struct HidHidePhysicalIdentity {std::string baseContainerDevicePath;};
static struct {bool active=true;HidHidePhysicalIdentity physical;std::vector<std::wstring> hiddenByUs{L"HID_FIXTURE_OWNED"};} g_hidHideVisibilityJournal;
static HidHidePhysicalIdentity g_hidHidePhysicalIdentity;
static std::atomic<bool> g_gamebarTraceAvailable{true},g_gamebarTraceVisible{false},g_gamebarTraceInputRedirected{false};
static std::atomic<ULONGLONG> g_gamebarTraceObservedTick{1000},g_gamebarTraceVersion{1},g_gamebarTraceInitAttempts{1};
static std::atomic<bool> g_physicalReleaseWindowDutyActive{false};
static std::vector<std::wstring> g_physicalReleaseWindowBorrowedRoots;
// A fixture-only stage-store notification delegates actual std::atomic operations.
// It chooses the producer interleaving; real thread scheduling is not claimed.
static std::function<void()> fixtureIdlePublished;
class FixtureStageAtomic {
 std::atomic<int> value_;
public:
 explicit FixtureStageAtomic(int value):value_(value){}
 int load(std::memory_order order=std::memory_order_seq_cst)const{return value_.load(order);}
 void store(int value,std::memory_order order=std::memory_order_seq_cst){value_.store(value,order);if(value==0&&fixtureIdlePublished){auto work=std::move(fixtureIdlePublished);fixtureIdlePublished={};work();}}
 bool compare_exchange_strong(int& expected,int desired,std::memory_order order=std::memory_order_seq_cst){return value_.compare_exchange_strong(expected,desired,order);}
 FixtureStageAtomic& operator=(int value){store(value);return *this;}
};
@@DECLARATIONS@@
@@EXTRA_DECLARATIONS@@
static bool hidHideRun(const std::wstring& text,std::string* raw,DWORD* exitCode,ULONGLONG){
 if(text.rfind(L"--dev-unhide",0)==0)++unhideCalls;else if(text.rfind(L"--dev-hide",0)==0)++hideCalls;
 else if(text==L"--dev-list"){if(raw)*raw="owned-list";if(exitCode)*exitCode=ERROR_SUCCESS;}
 else throw std::runtime_error("unadmitted memory CLI operation");return true;
}
static json physicalReleaseWindowRunCycles(const std::vector<std::wstring>&,ULONGLONG,const char*,bool* ok,std::vector<std::wstring>* roots){++cycleCalls;*ok=true;roots->push_back(L"USB_FIXTURE_OWNED");return {{"cyclePlanned",1}};}
static bool ymccOpenXInputAvailable(){return false;}
static bool ymccOpenXInputAssignSlot(const std::wstring&,int,bool& ok){++assignCalls;ok=true;return true;}
static json physicalReleaseWindowGameBarProcDiag(){return json::object();}
static void gamebarTraceEnsureInitIfDue(){++initCalls;}
static void gamebarTraceRefresh(const char*){if(preOpenInterleave){auto f=std::move(preOpenInterleave);f();}}
static void physicalReleaseWindowDutySupersedeByNewBorrow(){++supersedeCalls;}
static void physicalReleaseWindowRequest(const char*);
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
 for(const auto* mode:{"close-no-new-request","close-new-request-at-idle-publication","close-active-refresh-keeps-new-request-pending"}) {
  fixtureIdlePublished={};reset();g_physicalReleaseWindowDuty=PhysicalReleaseWindowCloseDuty{};refreshCalls=0;
  refreshAccepted=std::string(mode)=="close-active-refresh-keeps-new-request-pending";
  bool pass=false;std::string error;unsigned long long newToken=0;int afterCloseStage=-1,afterCloseTransition=-1;unsigned long long afterCloseToken=0;
  try {
   physicalReleaseWindowToggleRequest("old-oem");
   if(queued.size()!=1)throw std::runtime_error("actual initial Toggle did not queue Open");
   auto first=std::move(queued.front());queued.clear();first();
   if(g_physicalReleaseWindowStage.load()!=1||unhideCalls!=1||cycleCalls!=1)throw std::runtime_error("actual first borrow did not open");
   fixtureTick=3000;physicalReleaseWindowRequest("actual-test-close");
   if(queued.size()!=1||g_physicalReleaseWindowStage.load()!=2)throw std::runtime_error("actual Request did not queue Close");
   if(std::string(mode)!="close-no-new-request")fixtureIdlePublished=[&]{physicalReleaseWindowToggleRequest("new-current-oem");newToken=g_physicalReleaseWindowTokenId.load();};
   auto close=std::move(queued.front());queued.clear();close();
   afterCloseStage=g_physicalReleaseWindowStage.load();afterCloseTransition=g_physicalReleaseWindowTransition.load();afterCloseToken=g_physicalReleaseWindowTokenId.load();
   if(refreshAccepted) {
    if(!queued.empty()||g_physicalReleaseWindowPendingUntil!=3500||!g_physicalReleaseWindowDutyActive||!g_physicalReleaseWindowNeutral)throw std::runtime_error("active refresh did not retain its obligation and defer new request");
   }else if(std::string(mode)=="close-new-request-at-idle-publication") {
    if(queued.size()!=1)throw std::runtime_error("actual new Toggle was not admitted at published idle");
    auto next=std::move(queued.front());queued.clear();next();
    if(g_physicalReleaseWindowStage.load()!=1||!g_physicalReleaseWindowNeutral||unhideCalls!=2||cycleCalls!=2)throw std::runtime_error("old Close overwrote new queued owner or left stage2 stuck");
   }else if(g_physicalReleaseWindowStage.load()!=0||g_physicalReleaseWindowNeutral||!queued.empty())throw std::runtime_error("ordinary Close no longer ends idle");
   if(hideCalls!=1||refreshCalls!=1)throw std::runtime_error("ordinary Close IO budget/refresh attempt changed");
   pass=true;
  }catch(const std::exception& e){error=e.what();}
  cases.push_back({{"name",mode},{"pass",pass},{"error",error},{"newQueuedToken",newToken},{"tokenAfterOldClose",afterCloseToken},
   {"stageAfterOldClose",afterCloseStage},{"transitionAfterOldClose",afterCloseTransition},{"finalStage",g_physicalReleaseWindowStage.load()},
   {"finalTransition",g_physicalReleaseWindowTransition.load()},{"neutral",g_physicalReleaseWindowNeutral.load()},
   {"unhideCalls",unhideCalls},{"hideCalls",hideCalls},{"cycleCalls",cycleCalls},{"refreshCalls",refreshCalls},{"logs",logs}});
 }
 const bool ok=std::all_of(cases.begin(),cases.end(),[](const json& c){return c["pass"].get<bool>();});
 std::cout<<json{{"case","t1-close-publication"},{"pass",ok},{"cases",cases},{"actualFullCloseRequestToggleAndOpenBodies",true},
  {"stageDeclarationAdaptedForControlledNotification",true},{"realThreadScheduling",false},{"hardwareIo",false}}.dump();return ok?0:1;
}
