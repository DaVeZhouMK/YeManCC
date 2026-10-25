// Inert C++ harness: markers are filled with exact before/current source slices.
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
using json=nlohmann::json;
using DWORD=uint32_t;using ULONGLONG=uint64_t;using SHORT=int16_t;
struct XINPUT_GAMEPAD {uint16_t wButtons=0;uint8_t bLeftTrigger=0,bRightTrigger=0;SHORT sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
static constexpr DWORD ERROR_SUCCESS=0;
@@CONSTANTS@@
static ULONGLONG fakeTick=10000;
static ULONGLONG GetTickCount64(){return fakeTick;}
static unsigned long long currentPowerGeneration(){return 1;}
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
static std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}
static bool virtualOccupant=false,pathReadable=true,openAvailable=true;
static int pathQueries=0,availabilityQueries=0;
static bool ymccOpenXInputAvailable(){availabilityQueries++;return openAvailable;}
static std::wstring ymccOpenXInputGetDevicePath(DWORD){pathQueries++;return !pathReadable?L"":virtualOccupant?L"hid#HIDMAESTRO_VID_045E_PID_0B00":L"hid#physical_gamepad";}
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
static bool gamepadSerialScheduleFeedbackStop(unsigned long long,int){return true;}
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
 fakeTick+=10000;g_xinputTopologyEpoch=1;g_xinputSlotIdentityEpoch=1;g_xinputDiscoveryDirty=false;g_xinputSlotFullScanOnce=false;g_xinputSlotReprobeMask=0;
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
int main(){
 checks=json::array();
 // ROG/GPD labels cover the same non-MSI source path, not hardware claims.
 for(const std::string family:{"ROG","GPD"})for(const std::string trigger:{"dirty","boundary"})for(bool oldVirtual:{false,true}){
  reset();virtualOccupant=oldVirtual;sample();const int before=publications;
  virtualOccupant=!oldVirtual;discovery(trigger);
  for(int i=0;i<8;i++){fakeTick+=8;sample();}
  expect("new occupant controls live",latestLive,oldVirtual);
  expect("bounded single identity refresh",pathQueries,2);
  expect("actual source gate publication",publications-before,oldVirtual?8:0);
  finish(family+"-"+trigger+(oldVirtual?"-virtual-to-physical":"-physical-to-virtual"),true);
 }
 for(const std::string trigger:{"dirty","boundary"})for(bool sameVirtual:{false,true}){
  reset();virtualOccupant=sameVirtual;sample();discovery(trigger);sample();
  expect("same identity classification",latestLive,!sameVirtual);
  expect("same identity mask",g_xinputVirtualEndpointMask,sameVirtual?1:0);
  expect("one event refresh only",pathQueries,FIXTURE_EXPECT_REFRESH?2:1);
  finish(trigger+(sameVirtual?"-same-virtual":"-same-physical"));
 }
 for(bool isVirtual:{false,true}){
  reset();virtualOccupant=isVirtual;sample();for(int i=0;i<32;i++){fakeTick+=8;sample();}
  expect("stable ticks do not requery path",pathQueries,1);expect("stable classification",latestLive,!isVirtual);
  finish(isVirtual?"stable-virtual-no-discovery":"stable-physical-no-discovery");
 }
 {
  reset();pathReadable=false;sample();expect("unknown excluded",latestLive,false);expect("unknown mask",g_xinputSlotUnknownMask,1);expect("unknown clears probed",g_xinputVirtualEndpointProbed,0);expect("unknown sets existing deadline",g_xinputSlotReprobeAt[0],fakeTick+kXInputSlotReprobeBackoffMs);
  finish("unknown-unreadable-path-state");
  // Preserve the pre-existing literal behavior, including edgeProbe retry.
  fakeTick++;sample();expect("existing unknown edge retry preserved",pathQueries,2);expect("unknown still excluded",latestLive,false);finish("unknown-existing-edge-retry-behavior");
  pathReadable=true;xinputSlotsRequestIdentityReprobe(1,"fixture");sample();expect("explicit unknown recovery",latestLive,true);expect("unknown cleared",g_xinputSlotUnknownMask,0);finish("unknown-explicit-reprobe-recovery");
 }
 for(const std::string trigger:{"none","dirty","boundary"}){
  reset();g_xinputVirtualEndpointProbed=1;g_xinputSlotUnknownMask=1;g_xinputSlotReprobeAt[0]=fakeTick+500;
  // Seed the literal backoff branch; no claim that the unknown writer creates
  // this probed+unknown combination. Check unchanged branch semantics only.
  if(trigger!="none")discovery(trigger);sample();expect("seeded backoff does not query early",pathQueries,0);expect("seeded unknown not physical",latestLive,false);
  fakeTick+=500;sample();expect("existing deadline permits query",pathQueries,1);expect("existing recovery",latestLive,true);finish(trigger+"-seeded-backoff-branch");
 }
 for(const std::string trigger:{"dirty","boundary"}){
  reset();virtualOccupant=true;openAvailable=false;sample();discovery(trigger);sample();
  expect("no OpenXInput original physical compatibility",latestLive,true);expect("no unavailable path API",pathQueries,0);expect("unknown not invented",g_xinputSlotUnknownMask,0);finish(trigger+"-OpenXInput-unavailable-compatibility");
 }
 {
  reset();virtualOccupant=true;semanticNeutral=true;sample();expect("no source neutral cleanup allowed",latestLive,false);expect("neutral passes real source predicate",lastAccepted,true);finish("no-source-neutral-cleanup");
  semanticNeutral=false;sample();expect("no source live publication denied",lastAccepted,false);finish("no-source-nonneutral-denied");
 }
 for(const std::string trigger:{"none","dirty","boundary"}){
  reset();connected=false;fakeMsiBoard=true;g_msiDinputRouteDinputArmed=true;fakeMsiEligible=true;if(trigger!="none")discovery(trigger);sample();
  expect("DInput live",latestLive,true);expect("DInput source",latestSource,"msi-dinput");expect("DInput real source predicate",lastAccepted,true);expect("DInput pad",latestPad.wButtons,0x2000);expect("DInput no XInput path",pathQueries,0);expect("DInput permit balanced",permitsReleased,1);expect("DInput commit checked",commitChecks,1);finish(trigger+"-MSI-DInput-eligible");
 }
 for(const std::string failure:{"ineligible","permit-denied","snapshot-failed","commit-revoked"}){
  reset();connected=false;fakeMsiBoard=true;g_msiDinputRouteDinputArmed=true;fakeMsiEligible=failure!="ineligible";fakePermit=failure!="permit-denied";fakeCanonical=failure!="snapshot-failed";fakeCommit=failure!="commit-revoked";discovery("dirty");sample();
  expect("DInput existing rejection",lastAccepted,false);expect("DInput no live data",latestLive,false);expect("DInput no XInput path",pathQueries,0);expect("permit release preserved",permitsReleased,(failure=="snapshot-failed"||failure=="commit-revoked")?1:0);finish("MSI-DInput-"+failure);
 }
 {
  reset();connected=false;fakeMsiBoard=true;g_msiDinputRouteDinputArmed=true;fakeMsiEligible=false;semanticNeutral=true;sample();expect("MSI neutral allowed without eligible live source",lastAccepted,true);finish("MSI-neutral-cleanup-when-route-ineligible");
 }

 for(const std::string trigger:{"dirty","boundary"}){
  reset();sample();const auto captured=nativePhysicalFeedbackReady();auto ticket=g_touchHapticDispatch.request(1,true,fakeTick,1,ymcc::touchhaptic::kStrengthPercent,captured);if(!ticket)throw std::runtime_error("Ticket seed failed");
  const auto before=g_feedbackSlotGeneration[0];discovery(trigger);sample();
  expect("feedback generation event delta",g_feedbackSlotGeneration[0]-before,0u);
  nativeRunTouchHapticTicket(*ticket,false);
  expect("dirty ticket already topology-bound",feedbackStarts,(trigger=="dirty")?0:1);
  fakeTick+=ymcc::touchhaptic::kMinIntervalMs;auto fresh=g_touchHapticDispatch.request(1,true,fakeTick,1,ymcc::touchhaptic::kStrengthPercent,nativePhysicalFeedbackReady());if(!fresh)throw std::runtime_error("Ticket recovery failed");const int starts=feedbackStarts;nativeRunTouchHapticTicket(*fresh,false);expect("fresh ticket resumes after event",feedbackStarts,starts+1);finish(trigger+"-feedback-ticket-generation-and-recovery");
 }
 {
  reset();sample();for(int i=0;i<3;i++){auto t=g_touchHapticDispatch.request(1,true,fakeTick,1,ymcc::touchhaptic::kStrengthPercent,nativePhysicalFeedbackReady());if(!t)throw std::runtime_error("Burst ticket seed failed");discovery("dirty");sample();nativeRunTouchHapticTicket(*t,false);fakeTick+=ymcc::touchhaptic::kMinIntervalMs;}
  expect("finite event burst drops bounded tickets",feedbackStarts,0);auto fresh=g_touchHapticDispatch.request(1,true,fakeTick,1,ymcc::touchhaptic::kStrengthPercent,nativePhysicalFeedbackReady());if(!fresh)throw std::runtime_error("Burst recovery seed failed");nativeRunTouchHapticTicket(*fresh,false);expect("no latched feedback block after burst",feedbackStarts,1);finish("feedback-finite-event-burst-no-persistent-ticket-hold");
 }
 for(const std::string trigger:{"dirty","boundary","none"}){
  reset();sample();g_feedbackPulseTarget.slot=0;g_feedbackPulseTarget.identityEpoch=g_xinputSlotIdentityEpoch.load();g_feedbackPulseTarget.sourceGeneration=g_feedbackSlotGeneration[0];g_feedbackPulseTarget.powerGeneration=1;
#if FIXTURE_EXPECT_REFRESH
 g_feedbackPulseTarget.xinputIdentity=g_xinputFeedbackIdentity[0].capture(g_feedbackPulseTarget.identityEpoch,1);
#endif
 g_feedbackPulseActive=true;g_rogVibratePulseGen++;
  XINPUT_VIBRATION initial{17000,17000};g_feedbackSetState(0,&initial);if(trigger!="none")discovery(trigger);sample();physicalFeedbackFinish(g_rogVibratePulseGen);
  expect("existing finish clears job ownership",g_feedbackPulseActive,false);
  expect("same known identity delivers physical zero",zeroMotorWrites,1);
  expect("fake motor records actual stop side effect",fakeMotor,0);
  finish(trigger+"-feedback-active-pulse-stop-observation");
 }

 int failed=0,failedChecks=0,totalChecks=0;for(const auto& c:cases){if(!c["passed"].get<bool>())failed++;for(const auto& k:c["checks"]){totalChecks++;if(!k["passed"].get<bool>())failedChecks++;}}
 std::cout<<json{{"feedbackBudgets",{{"pulseMs",ymcc::touchhaptic::kDurationMs},{"minIntervalMs",ymcc::touchhaptic::kMinIntervalMs},{"maxQueueAgeMs",ymcc::touchhaptic::kMaxQueueAgeMs}}},{"cases",cases},{"caseCount",cases.size()},{"checks",totalChecks},{"failedCases",failed},{"failedChecks",failedChecks},{"productionHostExecuted",false},{"realDeviceOrSdkAccess",false}}.dump(2)<<std::endl;
 return failed?1:0;
}
