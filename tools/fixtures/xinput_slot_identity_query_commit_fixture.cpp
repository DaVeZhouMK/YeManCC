// Exact production path-query commit slices with a deterministic blocked API.
// No Windows, HIDMaestro, Host, service or device libraries are linked.
#include "json.hpp"
#include "touch_haptic_dispatch.h"
#include "xinput_identity_refresh_policy.h"
#include <atomic>
#include <memory>
#include <mutex>
#include <array>
#include <string>
#include <vector>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <condition_variable>
#include <chrono>
#include <functional>
using json=nlohmann::json;
using DWORD=uint32_t;using ULONGLONG=uint64_t;using SHORT=int16_t;
struct XINPUT_GAMEPAD {uint16_t wButtons=0;uint8_t bLeftTrigger=0,bRightTrigger=0;SHORT sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
static constexpr DWORD ERROR_SUCCESS=0;
@@CONSTANTS@@
static ULONGLONG fakeTick=10000;
static ULONGLONG GetTickCount64(){return fakeTick;}
static std::atomic<unsigned long long> fakePower{1};
static unsigned long long currentPowerGeneration(){return fakePower.load(std::memory_order_acquire);}
static bool tickCostEnabled(){return false;}
static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1};
static std::atomic<bool> g_xinputDiscoveryDirty{false},g_xinputSlotFullScanOnce{false};
static std::atomic<unsigned long long> g_xinputDiscoveryDirtyTick{0};
static std::atomic<DWORD> g_xinputSlotReprobeMask{0};
static DWORD g_xinputConnectedMask=1,g_xinputVirtualEndpointMask=0,g_xinputVirtualEndpointProbed=0,g_xinputSlotUnknownMask=0,g_xinputAdmittedMask=0;
static DWORD g_xinputSlotIdentityResolvedEpoch[4]={};
static ULONGLONG g_xinputSlotReprobeAt[4]={};
static unsigned long long g_feedbackSlotGeneration[4]={};
static bool g_xinputPhysicalSourceLocked=true;static DWORD g_xinputPhysicalSourceMask=1,g_xinputPrimarySlot=0,g_xinputSemanticSlot=0;
static std::array<XINPUT_GAMEPAD,4> g_xinputPads;static std::array<bool,4> g_xinputSpecialButtons;static bool g_xinputSpecialButton=false;
static std::atomic<bool> g_externalPriorityVirtualSuspended{false};
static std::atomic<unsigned> g_externalPrioritySourceTopology{0};
static std::atomic<unsigned long long> g_externalPrioritySourcePower{0},g_externalPrioritySourceDeadline{0};
static std::atomic<DWORD> g_externalPrioritySourceMask{0};
static bool fakeMsiBoard=false,fakeMsiEligible=false;
static std::atomic<bool> g_msiDinputRouteDinputArmed{false};
static bool msiClawKnownBoardAuthority(){return fakeMsiBoard;}
static bool msiDinputMainSourceEligible(){return fakeMsiEligible;}
static void appendNativeLifecycleLog(const char*,const json&){}
static std::string W2U(const std::wstring& s){std::string r;for(const auto c:s){if(c<0||c>127)throw std::logic_error("Fixture path must be ASCII");r.push_back(static_cast<char>(c));}return r;}
static bool virtualOccupant=false,pathReadable=true,openAvailable=true;
static int pathQueries=0,availabilityQueries=0;
static bool ymccOpenXInputAvailable(){availabilityQueries++;return openAvailable;}
static std::mutex pathMx;static std::condition_variable pathCv;
static bool blockPath=false,pathEntered=false,pathReleased=false;static int blockedQueries=0;
static std::wstring ymccOpenXInputGetDevicePath(DWORD){
 pathQueries++;const std::wstring answer=!pathReadable?L"":virtualOccupant?L"hid#HIDMAESTRO_VID_045E_PID_0B00":L"hid#physical_gamepad";
 std::unique_lock<std::mutex> lock(pathMx);
 if(blockPath){pathEntered=true;blockedQueries++;pathCv.notify_all();if(!pathCv.wait_for(lock,std::chrono::seconds(5),[]{return pathReleased;}))throw std::runtime_error("Path release timeout");blockPath=false;}
 return answer;
}
@@IDENTITY_ENUM@@
@@METHODS@@
namespace ymcc::msi::virtualReportDiag {struct SourceStamp {};}
struct MsiSamplePermit {bool granted=false;};
static bool fakePermit=true,fakeCanonical=true,fakeCommit=true;
static int permitsAcquired=0,permitsReleased=0,canonicalReads=0,commitChecks=0;
static MsiSamplePermit msiSampleAcquirePermit(){permitsAcquired++;return {fakePermit};}
static bool msiDinputCanonicalSnapshot(XINPUT_GAMEPAD& pad
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 ,ymcc::msi::virtualReportDiag::SourceStamp*
#endif
){canonicalReads++;pad={0x2000,0,0,0,0,-16000,0};return fakeCanonical;}
static bool msiSampleCommitAllowed(const MsiSamplePermit&){commitChecks++;return fakeCommit;}
static void msiSampleReleasePermit(const MsiSamplePermit&){permitsReleased++;}
static XINPUT_GAMEPAD xinputMergeConnectedPads(DWORD,DWORD&,bool&){throw std::logic_error("Unexpected permissive merge");}

using BYTE=uint8_t;struct XINPUT_VIBRATION {uint16_t wLeftMotorSpeed=0,wRightMotorSpeed=0;};
struct HidHideMsiMainSourceRecord {unsigned long long generation=0;};
static int scheduledStopNotifications=0;
static bool gamepadSerialScheduleFeedbackStop(unsigned long long,int){scheduledStopNotifications++;return true;}
@@IDENTITY_POLICY@@
@@FEEDBACK_TARGET@@
static std::mutex g_feedbackPulseMx,g_gamepadReadMx;
static PhysicalFeedbackTarget g_feedbackPulseTarget;
static bool g_feedbackPulseActive=false,g_feedbackPulseTouch=false;
static unsigned long long g_rogVibratePulseGen=0;
static std::atomic<unsigned long long> g_msiFeedbackModeGeneration{0},g_msiFeedbackSourceGeneration{0},g_msiFeedbackSamplePower{0},g_msiFeedbackSampleMode{0},g_msiFeedbackSampleTopology{0};
static std::atomic<bool> g_msiDinputSourceValid{false},g_exitRequested{false};
static bool hardwareWriteAllowed(){return true;}
static int fakeMotor=0,zeroMotorWrites=0;
static DWORD fakeFeedbackSetState(DWORD,XINPUT_VIBRATION* v){fakeMotor=v->wRightMotorSpeed;if(!v->wLeftMotorSpeed&&!v->wRightMotorSpeed)zeroMotorWrites++;return 0;}
static auto g_feedbackSetState=fakeFeedbackSetState;
static int msiDinputFeedbackWrite(BYTE,BYTE,bool,const char*,const HidHideMsiMainSourceRecord*,bool,const ymcc::touchhaptic::SourceToken*){return 0;}
@@FEEDBACK_METHODS@@
static ymcc::touchhaptic::Dispatch g_touchHapticDispatch;
static ymcc::touchhaptic::Summary g_touchHapticSummary;
static int feedbackStarts=0;
static ymcc::touchhaptic::Receipt nativeScheduleTouchFeedback(int strength,const ymcc::touchhaptic::SourceToken*){feedbackStarts++;ymcc::touchhaptic::Receipt r;r.ok=true;r.backend=ymcc::touchhaptic::Backend::XInput;r.strength=strength;return r;}
@@FEEDBACK_TICKET@@

static bool latestLive=false;static std::string latestSource="none";static XINPUT_GAMEPAD latestPad;
static bool connected=true,semanticNeutral=false;static int publications=0;
static bool lastAccepted=false;
static void sample(){
 const ULONGLONG probeNow=GetTickCount64();
 @@READ_PREFIX@@
 bool live=false;XINPUT_GAMEPAD pad{};std::string inputSource="none";
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
 ymcc::msi::virtualReportDiag::SourceStamp msiReportSource{};
#endif
 g_xinputConnectedMask=connected?1:0;g_xinputPads.fill({});
 if(connected){
  // The canonical fake GetState succeeds, even during occupant replacement.
  const DWORD i=0;g_xinputPads[i]={0x1000,0,0,0,0,16000,0};
  @@CLASSIFICATION@@
 }
 @@ADMISSION@@
 @@SELECTION@@
 @@MSI_FALLBACK@@
 latestLive=live;latestSource=inputSource;latestPad=pad;
 bool submitNeutral=semanticNeutral;
 const bool physicalSourceAdmitted=(@@LIVE_OR_NEUTRAL@@);
 lastAccepted=inputHostPhysicalPublicationSourceAdmitted(physicalSourceAdmitted,submitNeutral);
 if(lastAccepted)publications++;
}
static void reset(){
 fakeTick+=10000;fakePower=1;scheduledStopNotifications=0;g_xinputTopologyEpoch=1;g_xinputSlotIdentityEpoch=1;g_xinputDiscoveryDirty=false;g_xinputSlotFullScanOnce=false;g_xinputSlotReprobeMask=0;
 g_xinputConnectedMask=1;g_xinputVirtualEndpointMask=0;g_xinputVirtualEndpointProbed=0;g_xinputSlotUnknownMask=0;g_xinputAdmittedMask=0;
 std::fill(std::begin(g_xinputSlotIdentityResolvedEpoch),std::end(g_xinputSlotIdentityResolvedEpoch),0);
 std::fill(std::begin(g_xinputSlotReprobeAt),std::end(g_xinputSlotReprobeAt),0);
 g_xinputPrimarySlot=0;g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;
 fakeMsiBoard=false;fakeMsiEligible=false;g_msiDinputRouteDinputArmed=false;
 virtualOccupant=false;pathReadable=true;openAvailable=true;pathQueries=0;availabilityQueries=0;
 permitsAcquired=0;permitsReleased=0;canonicalReads=0;commitChecks=0;fakePermit=true;fakeCanonical=true;fakeCommit=true;
 g_feedbackPulseActive=false;g_feedbackPulseTarget={};
#if FIXTURE_EXPECT_REFRESH
 for(auto& entry:g_xinputFeedbackIdentity)entry=ymcc::input::identityRefresh::Slot{};g_xinputDeferredFeedbackStop.clear();
#endif
fakeMotor=0;zeroMotorWrites=0;feedbackStarts=0;g_touchHapticSummary={};g_touchHapticDispatch.request(0,false,fakeTick,1);latestLive=false;latestSource="none";latestPad={};connected=true;semanticNeutral=false;publications=0;lastAccepted=false;
}
static void discovery(const std::string& trigger){if(trigger=="dirty")xinputSlotsMarkDiscoveryDirty();else if(trigger=="boundary")g_xinputSlotFullScanOnce=true;else throw std::logic_error("Invalid discovery trigger");}
static json signature(){return {{"live",latestLive},{"source",latestSource},{"padButtons",latestPad.wButtons},{"padRightX",latestPad.sThumbRX},{"physicalMask",xinputPhysicalConnectedMask()},{"admittedMask",g_xinputAdmittedMask},{"accepted",lastAccepted},{"publications",publications},{"permitsAcquired",permitsAcquired},{"permitsReleased",permitsReleased},{"canonicalReads",canonicalReads},{"commitChecks",commitChecks}};}
static json checks;
static void expect(const char* name,json actual,json expected){checks.push_back({{"check",name},{"actual",actual},{"expected",expected},{"passed",actual==expected}});}
static json cases=json::array();
static void finish(const std::string& name,bool oldRegression=false){bool passed=std::all_of(checks.begin(),checks.end(),[](const auto& c){return c["passed"].template get<bool>();});cases.push_back({{"name",name},{"oldRegressionWitness",oldRegression},{"passed",passed},{"checks",checks},{"signature",signature()},{"pathQueries",pathQueries}});checks=json::array();}

// The API blocks on the sampling thread. A second thread advances only atomic
// event state, then releases it. Condition-variable synchronization orders the
// event before the query return; no sleeps or hardware mapping calls are used.
static void blockedSample(const std::function<void()>& change){
 {std::lock_guard<std::mutex> lock(pathMx);blockPath=true;pathEntered=false;pathReleased=false;}
 std::thread event([&]{
  std::unique_lock<std::mutex> lock(pathMx);
  if(!pathCv.wait_for(lock,std::chrono::seconds(5),[]{return pathEntered;})){pathReleased=true;lock.unlock();pathCv.notify_all();return;}
  change();pathReleased=true;lock.unlock();pathCv.notify_all();
 });
 try{sample();}catch(...){event.join();throw;}event.join();
}
static void changeEpoch(int kind){
 if(kind==0)xinputSlotsRequestIdentityReprobe(1,"blocked-query-test");
 else if(kind==1)fakePower.fetch_add(1,std::memory_order_release);
 else xinputSlotsMarkDiscoveryDirty();
}
static void seedStopDebt(){(void)g_xinputDeferredFeedbackStop.plan(17,0,fakeTick,1);(void)g_xinputDeferredFeedbackStop.plan(17,0,fakeTick+1,1);}
int main(){checks=json::array();
 for(const bool virtualSource:{false,true}){
  reset();virtualOccupant=virtualSource;sample();for(int i=0;i<8;i++)sample();expect("stable classification",lastAccepted,!virtualSource);expect("stable no path polling",pathQueries,1);finish(virtualSource?"stable-virtual":"stable-physical");
 }
 for(const bool nextVirtual:{false,true}){
  reset();virtualOccupant=!nextVirtual;sample();virtualOccupant=nextVirtual;discovery("dirty");sample();expect("event before query converges",lastAccepted,!nextVirtual);expect("one event query",pathQueries,2);finish(nextVirtual?"prior-dirty-to-virtual":"prior-dirty-to-physical");
 }
 const char* kinds[]={"identity-epoch","power","topology"};
 for(int kind=0;kind<3;kind++)for(const bool nextVirtual:{false,true})for(const bool pendingDebt:{false,true}){
  reset();sample();if(pendingDebt)seedStopDebt();discovery("dirty");const int before=publications;
  blockedSample([&]{changeEpoch(kind);virtualOccupant=nextVirtual;});
  const auto rejected=signature();const auto resolved=g_xinputSlotIdentityResolvedEpoch[0];
  expect("invalid query excluded from Physical",xinputPhysicalConnectedMask(),0);
  expect("invalid query excluded from admitted source",g_xinputAdmittedMask,0);
  expect("source gate rejects current round",lastAccepted,false);
  expect("no current non-neutral publication",publications-before,0);
  expect("invalid query never signed with a new epoch",resolved,0);
  expect("requery responsibility survives",bool(g_xinputSlotReprobeMask.load()&1),true);
  if(pendingDebt)expect("old stop debt remains owned",g_xinputDeferredFeedbackStop.pendingFor(0),17);
  sample();expect("next existing sample re-queries",pathQueries,3);
  expect("next query converges",lastAccepted,!nextVirtual);
  expect("next physical mask reflects actual occupant",xinputPhysicalConnectedMask(),nextVirtual?0:1);
  expect("next commit uses current epoch",g_xinputSlotIdentityResolvedEpoch[0],g_xinputSlotIdentityEpoch.load());
  if(pendingDebt){expect("next probe preserves stop debt",g_xinputDeferredFeedbackStop.pendingFor(0),17);expect("resolution notifies existing stop owner",scheduledStopNotifications>0,true);}
  finish(std::string("blocked-")+kinds[kind]+(nextVirtual?"-next-virtual":"-next-physical")+(pendingDebt?"-stop-debt":""),true);
  cases.back()["invalidRound"]=rejected;cases.back()["resolvedEpochDuringInvalidRound"]=resolved;
 }
 for(int kind=0;kind<3;kind++){
  reset();sample();semanticNeutral=true;discovery("dirty");blockedSample([&]{changeEpoch(kind);});
  expect("neutral release remains accepted",lastAccepted,true);finish(std::string("blocked-")+kinds[kind]+"-neutral-release");
 }
 for(int kind=0;kind<3;kind++){
  reset();sample();discovery("dirty");blockedSample([&]{changeEpoch(kind);virtualOccupant=true;});
  virtualOccupant=false;discovery("dirty");sample();
  expect("finite invalid event recovers a physical source",lastAccepted,true);expect("finite event requery count",pathQueries,3);finish(std::string("finite-")+kinds[kind]+"-recovery");
 }
 reset();pathReadable=false;sample();expect("empty path is not Physical",lastAccepted,false);pathReadable=true;sample();expect("unknown resolution still recovers",lastAccepted,true);finish("unknown-path-recovery");
 reset();openAvailable=false;sample();expect("legacy unavailable Physical compatibility",lastAccepted,true);expect("legacy makes no path query",pathQueries,0);finish("legacy-openxinput-unavailable");
 reset();openAvailable=false;sample();seedStopDebt();discovery("dirty");sample();expect("legacy stop owner retained",g_xinputDeferredFeedbackStop.pendingFor(0),17);expect("legacy physical remains accepted",lastAccepted,true);expect("legacy stop owner notified",scheduledStopNotifications>0,true);finish("legacy-stop-debt");
 reset();sample();seedStopDebt();discovery("dirty");sample();expect("stable same device retains stop debt",g_xinputDeferredFeedbackStop.pendingFor(0),17);expect("stable same device remains Physical",lastAccepted,true);finish("stable-feedback-stop-debt");
 reset();sample();g_feedbackPulseTarget.slot=0;g_feedbackPulseTarget.identityEpoch=g_xinputSlotIdentityEpoch.load();g_feedbackPulseTarget.sourceGeneration=g_feedbackSlotGeneration[0];g_feedbackPulseTarget.powerGeneration=currentPowerGeneration();g_feedbackPulseTarget.xinputIdentity=g_xinputFeedbackIdentity[0].capture(g_feedbackPulseTarget.identityEpoch,currentPowerGeneration());g_feedbackPulseActive=true;g_rogVibratePulseGen++;XINPUT_VIBRATION initial{17000,17000};g_feedbackSetState(0,&initial);discovery("dirty");sample();physicalFeedbackFinish(g_rogVibratePulseGen);expect("same identity pulse clears ownership",g_feedbackPulseActive,false);expect("same identity stops fake motor",zeroMotorWrites,1);expect("same identity fake motor is zero",fakeMotor,0);finish("same-device-feedback-finish");
 int failed=0,total=0,failedChecks=0;for(const auto& c:cases){if(!c["passed"].get<bool>())failed++;for(const auto& q:c["checks"]){total++;if(!q["passed"].get<bool>())failedChecks++;}}
 std::cout<<json{{"cases",cases},{"caseCount",cases.size()},{"checks",total},{"failedCases",failed},{"failedChecks",failedChecks},{"blockedQueries",blockedQueries},{"sourceGateScope","exact source predicate, no Host transport"},{"productionHostExecuted",false},{"realDeviceOrSdkAccess",false}}.dump(2)<<std::endl;return failed?1:0;
}
