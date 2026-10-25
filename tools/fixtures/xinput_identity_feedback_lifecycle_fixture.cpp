// Pure local harness. Production functions/blocks are inserted verbatim by the
// driver; only XInput/MSI, time, scheduler wake and source observations are fake.
#include "json.hpp"
#include "touch_haptic_dispatch.h"
#include "xinput_identity_refresh_policy.h"
#if FIXED_POLICY && defined(YMCC_CURRENT_GAME_FEEDBACK_CONTEXT)
#include "game_feedback_dispatch.h"
#include "game_feedback_diagnostics.h"
#endif
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cctype>
#include <cwctype>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using json=nlohmann::json;
using DWORD=uint32_t;using WORD=uint16_t;using BYTE=uint8_t;using ULONGLONG=uint64_t;
constexpr DWORD ERROR_SUCCESS=0;
#if FIXED_POLICY && defined(YMCC_UNIFIED_PHYSICAL_FEEDBACK)
#include "touch_feedback_lease.h"
#include "xinput_feedback_msi_platform.inc"
#endif
struct XINPUT_VIBRATION {WORD wLeftMotorSpeed=0,wRightMotorSpeed=0;};
static std::atomic<ULONGLONG> fakeTick{1000},fakePower{1};
static ULONGLONG GetTickCount64(){return fakeTick.load();}
static auto currentPowerGeneration(){return fakePower.load();}
static std::atomic<unsigned> g_xinputSlotIdentityEpoch{1},g_xinputTopologyEpoch{1};
static std::atomic<bool> g_xinputDiscoveryDirty{false},g_exitRequested{false};
static std::atomic<unsigned long long> g_xinputDiscoveryDirtyTick{0};
static std::atomic<DWORD> g_xinputSlotReprobeMask{0};
static DWORD g_xinputConnectedMask=1,g_xinputVirtualEndpointMask=0,g_xinputVirtualEndpointProbed=0,g_xinputSlotUnknownMask=0,g_xinputAdmittedMask=1;
static DWORD g_xinputSlotIdentityResolvedEpoch[4]={};
static ULONGLONG g_xinputSlotReprobeAt[4]={};
static unsigned long long g_feedbackSlotGeneration[4]={};
static bool g_xinputPhysicalSourceLocked=true;static DWORD g_xinputPhysicalSourceMask=1,g_xinputPrimarySlot=0;
static constexpr ULONGLONG kXInputSlotReprobeBackoffMs=500;
static bool tickCostEnabled(){return false;}
static json pendingLog=json::array();static bool throwPendingLog=false;
static void appendNativeLifecycleLog(const char* event,const json& row){if(std::string(event)=="gamepad-feedback-stop-pending"){if(throwPendingLog)throw std::runtime_error("fixture logger");pendingLog.push_back(row);}}
static std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}
static bool openAvailable=true,pathReadable=true,connected=true,writeAllowed=true;
static std::wstring occupant=L"HID#physical-A";
static int pathQueries=0,availabilityQueries=0;
static std::function<void()> inProbe;
static bool ymccOpenXInputAvailable(){++availabilityQueries;return openAvailable;}
static std::wstring ymccOpenXInputGetDevicePath(DWORD){++pathQueries;auto result=pathReadable?occupant:L"";if(inProbe)inProbe();return result;}
@@IDENTITY_ENUM@@
@@METHODS@@
#if FIXED_POLICY && defined(YMCC_UNIFIED_PHYSICAL_FEEDBACK)
@@MSI_SOURCE@@
#else
struct HidHideMsiMainSourceRecord {unsigned long long generation=7;};
static std::shared_ptr<const HidHideMsiMainSourceRecord> msiSource;
static auto msiDinputFeedbackSourceSnapshot(){return msiSource;}
#endif
static std::atomic<unsigned long long> g_msiFeedbackModeGeneration{1},g_msiFeedbackSourceGeneration{0},g_msiFeedbackSamplePower{1},g_msiFeedbackSampleMode{1},g_msiFeedbackSampleTopology{1};
static std::atomic<bool> g_msiDinputSourceValid{false};
static struct {std::string deviceClass="ROG";} g_machineIdentity;
static bool hardwareWriteAllowed(){return writeAllowed;}
static bool returnTapMsi=false;
static bool msiClawKnownBoardAuthority(){
#if FIXED_POLICY && defined(YMCC_UNIFIED_PHYSICAL_FEEDBACK)
 return returnTapMsi||(g_msiMainSource.valid&&fixtureMsiAuthority);
#else
 return returnTapMsi;
#endif
}
static int msiMotor=0,msiZeros=0,msiWrites=0;
#if FIXED_POLICY && defined(YMCC_CURRENT_GAME_FEEDBACK_CONTEXT)
#include "xinput_identity_feedback_game_memory.inc"
@@GAME_COMMAND@@
#endif
#if FIXED_POLICY && defined(YMCC_UNIFIED_PHYSICAL_FEEDBACK)
@@MSI_NATIVE@@
static void fixturePublishMsiSource(){auto record=*msiSource;record.valid=true;record.generation=7;record.interfacePath="HID#MSI-main-game";g_msiMainSource=record;g_msiFeedbackSourceRecord.reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>(record);}
static int fixtureMsiMotor(){if(MsiFeedbackLeaseOps::fixtureWrites)return fixtureMsiNativeReports.empty()?0:fixtureMsiNativeReports.back()["large"].get<int>();return fixtureMsiSdkReports.empty()?0:fixtureMsiSdkReports.back()[5].get<int>();}
static int fixtureMsiWrites(){return (int)(MsiFeedbackLeaseOps::fixtureWrites?fixtureMsiNativeReports.size():fixtureMsiSdkReports.size());}
static int fixtureMsiZeros(){int zeros=0;for(const auto& r:fixtureMsiNativeReports)if(r["small"]==0&&r["large"]==0)++zeros;for(const auto& r:fixtureMsiSdkReports)if(r[4]==0&&r[5]==0)++zeros;return zeros;}
#else
struct MsiWrite {bool ok=true,attempted=true,written=true;DWORD lastError=0;ymcc::touchhaptic::Reason reason=ymcc::touchhaptic::Reason::None;};
static MsiWrite msiDinputFeedbackWrite(BYTE left,BYTE right,bool,const char*,const HidHideMsiMainSourceRecord*,bool,const ymcc::touchhaptic::SourceToken*){++msiWrites;msiMotor=right;if(!left&&!right)++msiZeros;return {};}
static void fixturePublishMsiSource(){}
static int fixtureMsiMotor(){return msiMotor;}static int fixtureMsiZeros(){return msiZeros;}static int fixtureMsiWrites(){return msiWrites;}
#endif
static DWORD g_xinputKnownSlotMask=1;
static std::mutex g_feedbackPulseMx,g_gamepadReadMx,g_gamepadSerialMx;
static std::condition_variable g_gamepadSerialCv;
#if FIXED_POLICY && defined(YMCC_CURRENT_GAME_FEEDBACK_CONTEXT)
@@GAME_DISPATCH@@
#endif
static bool g_gamepadSerialStopping=false;
struct FeedbackStopDeadline {unsigned long long generation=0,due=0;};
static std::optional<FeedbackStopDeadline> g_gamepadSerialFeedbackStop;
static void gamepadSerialStartLocked(){}
static bool gamepadSerialScheduleFeedbackStop(unsigned long long,int);
@@POLICY_HELPER@@
@@MSI_RETRY@@
@@TARGET@@
static PhysicalFeedbackTarget g_feedbackPulseTarget;
static bool g_feedbackPulseActive=false,g_feedbackPulseTouch=false;
static unsigned long long g_rogVibratePulseGen=0;
static std::map<std::wstring,int> motors;
static json writes=json::array();
static unsigned zeroFailures=0,nonzeroFailures=0;
// A device/transport may report success while its output latch is stale.
// The fault only changes final physical-output observation, never source admission.
static bool outputLatchNeedsZero=false;
static std::function<void()> afterFeedbackWrite;
static DWORD fakeSetState(DWORD slot,XINPUT_VIBRATION* v){
 const bool zero=!v->wLeftMotorSpeed&&!v->wRightMotorSpeed;
 if(zero&&zeroFailures){--zeroFailures;return 5;}
 if(!zero&&nonzeroFailures){--nonzeroFailures;return 5;}
 if(zero)outputLatchNeedsZero=false;
 if(!outputLatchNeedsZero)motors[occupant]=v->wRightMotorSpeed;
 writes.push_back({{"slot",slot},{"occupant",W2U(occupant)},{"left",v->wLeftMotorSpeed},{"right",v->wRightMotorSpeed}});
 if(afterFeedbackWrite){auto callback=std::move(afterFeedbackWrite);afterFeedbackWrite={};callback();}
 return ERROR_SUCCESS;
}
static auto g_feedbackSetState=fakeSetState;
@@CURRENT@@
@@STOP@@
@@FINISH@@
@@READY@@
@@START@@
@@RECEIPT@@
@@SCHEDULE@@
static void sample(bool dirtyDiscovery=false,bool boundaryFullScan=false){
 std::lock_guard<std::mutex> lock(g_gamepadReadMx);
 const auto probeNow=GetTickCount64();const DWORD i=0;
 // Production classification now validates the context captured at read start.
 const auto diagnosticReadPower=currentPowerGeneration();
 const auto diagnosticReadTopology=g_xinputTopologyEpoch.load(std::memory_order_acquire);
 g_xinputConnectedMask=connected?1:0;
 if(connected){
  g_xinputKnownSlotMask|=1;
  @@CLASSIFICATION@@
 }else if(g_xinputKnownSlotMask&1u){
  @@DISCONNECT@@
 }
}
static bool pump(){std::optional<FeedbackStopDeadline> stop;{std::lock_guard<std::mutex> l(g_gamepadSerialMx);if(g_gamepadSerialFeedbackStop&&g_gamepadSerialFeedbackStop->due<=GetTickCount64()){stop=g_gamepadSerialFeedbackStop;g_gamepadSerialFeedbackStop.reset();}}if(stop)physicalFeedbackFinish(stop->generation);return stop.has_value();}
static void reset(){
#if FIXED_POLICY && defined(YMCC_CURRENT_GAME_FEEDBACK_CONTEXT)
 g_gamepadSerialGameFeedback={};g_gamepadSerialGameInterested=false;g_gameMotorExecutionStats={};
 g_inputHostGameMotorBindingGeneration=0;g_inputHostVirtualIntentRevision=1;g_msiMainSourceRevision=0;
 g_inputReady=false;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_inputHostVirtualIntentWriters=0;fixtureInputHostEpoch=1;
#endif
#if FIXED_POLICY && defined(YMCC_UNIFIED_PHYSICAL_FEEDBACK)
 msiDinputInvalidateFeedbackLease();MsiFeedbackLeaseOps::fixtureWrites=&fixtureMsiNativeReports;fixtureMsiNativeReports=json::array();fixtureMsiSdkReports=json::array();g_msiMainSource={};g_msiFeedbackSourceRecord.reset();g_msiFeedbackIdentityBuilds=0;
 fixtureMsiOpens=fixtureMsiCloses=fixtureMsiCancels=fixtureMsiWaits=fixtureMsiSdkCalls=0;fixtureMsiLastError=0;fixtureMsiVid=0x0db0;fixtureMsiPid=0x1902;fixtureMsiUsagePage=1;fixtureMsiUsage=5;fixtureMsiReportLength=32;fixtureMsiAttributes=fixtureMsiCaps=fixtureMsiAuthority=true;fixtureMsiIo=FixtureMsiIo::Sync;fixtureMsiLastBuffer.clear();
 fixtureMsiBeforeOpen={};
#endif
 returnTapMsi=false;
 fakeTick=1000;fakePower=1;g_xinputSlotIdentityEpoch=1;g_xinputTopologyEpoch=1;g_xinputDiscoveryDirty=false;g_xinputSlotReprobeMask=0;g_exitRequested=false;
 g_xinputKnownSlotMask=1;g_xinputConnectedMask=1;g_xinputVirtualEndpointMask=0;g_xinputVirtualEndpointProbed=0;g_xinputSlotUnknownMask=0;g_xinputAdmittedMask=1;
 for(int i=0;i<4;++i){g_xinputSlotIdentityResolvedEpoch[i]=0;g_xinputSlotReprobeAt[i]=0;g_feedbackSlotGeneration[i]=0;
#if FIXED_POLICY
 g_xinputFeedbackIdentity[i]=ymcc::input::identityRefresh::Slot{};
#endif
 }
#if FIXED_POLICY
 g_xinputDeferredFeedbackStop.clear();
#endif
 g_xinputPhysicalSourceLocked=true;g_xinputPhysicalSourceMask=1;g_xinputPrimarySlot=0;
 g_feedbackPulseActive=false;g_feedbackPulseTouch=false;g_rogVibratePulseGen=0;g_feedbackPulseTarget={};g_gamepadSerialFeedbackStop.reset();g_gamepadSerialStopping=false;
 openAvailable=pathReadable=connected=writeAllowed=true;occupant=L"HID#physical-A";inProbe={};pathQueries=availabilityQueries=0;motors.clear();writes=json::array();
 pendingLog=json::array();throwPendingLog=false;zeroFailures=nonzeroFailures=0;outputLatchNeedsZero=false;afterFeedbackWrite={};msiSource.reset();g_msiFeedbackSourceGeneration=0;g_msiFeedbackModeGeneration=1;g_msiFeedbackSamplePower=1;g_msiFeedbackSampleMode=1;g_msiFeedbackSampleTopology=1;g_msiDinputSourceValid=false;msiMotor=msiZeros=msiWrites=0;g_machineIdentity.deviceClass="ROG";
}
static ymcc::touchhaptic::Receipt start(){auto receipt=nativePhysicalFeedbackStart(26,18,false);if(!receipt.ok)throw std::runtime_error("Pulse setup unexpectedly failed");if(!gamepadSerialScheduleFeedbackStop(receipt.pulseGeneration,18))throw std::runtime_error("Schedule failed");return receipt;}
static json cases=json::array(),checks=json::array();
template<class A,class B>static void expect(const char* name,A a,B b){checks.push_back({{"name",name},{"actual",a},{"expected",b},{"passed",a==b}});}
static void finish(const std::string& name){bool ok=true;for(const auto& c:checks)ok&=c["passed"].get<bool>();cases.push_back({{"name",name},{"passed",ok},{"checks",checks},{"motorWrites",writes}});checks=json::array();}
static int zerosFor(const std::wstring& who){int n=0;for(const auto& w:writes)if(w["occupant"]==W2U(who)&&w["left"]==0&&w["right"]==0)++n;return n;}
int main(){
 for(const std::string trigger:{"dirty","boundary","none"}){
  reset();sample();start();const auto generation=g_feedbackSlotGeneration[0];sample(trigger=="dirty",trigger=="boundary");fakeTick+=18;pump();
  expect("same identity preserves active zero",motors[L"HID#physical-A"],0);expect("owner reclaimed",g_feedbackPulseActive,false);expect("one zero",zerosFor(L"HID#physical-A"),1);
  if(trigger!="none")expect("refresh leaves generation",g_feedbackSlotGeneration[0],generation);finish("active-zero-"+trigger);
 }
 for(const std::wstring replacement:{L"HID#physical-B",L"HID#HIDMAESTRO_VID_045E_PID_0B00"}){
  reset();sample();auto r=start();occupant=replacement;sample(true);fakeTick+=18;pump();
  expect("old stop never reaches new occupant",zerosFor(replacement),0);expect("old owner reclaimed",g_feedbackPulseActive,false);
  const bool virt=replacement.find(L"HIDMAESTRO")!=std::wstring::npos;expect("correct source mask",xinputPhysicalConnectedMask(),virt?0u:1u);
  if(!virt){start();fakeTick+=18;pump();expect("replacement owns its own zero",zerosFor(replacement),1);expect("new pulse reclaimed",g_feedbackPulseActive,false);}
  finish(virt?"A-to-virtual":"A-to-different-physical");
 }
 {
  reset();sample();start();occupant=L"hid#PHYSICAL-a";sample(true);fakeTick+=18;pump();expect("Windows ASCII case equivalent",zerosFor(occupant),1);finish("same-path-case-normalization");
 }
 for(const std::string recovery:{"same","different","virtual","timeout","disconnect","provider"}){
  reset();sample();auto r=start();pathReadable=false;sample(true);fakeTick+=18;pump();
  expect("no blind zero during Unknown",zerosFor(occupant),0);expect("new Unknown pulse denied",nativePhysicalFeedbackReady().backend==ymcc::touchhaptic::Backend::None,true);
  expect("Unknown retains single owner",g_feedbackPulseActive,true);
  expect("Unknown has bounded deadline",g_gamepadSerialFeedbackStop.has_value(),true);
  if(recovery=="timeout"){
   fakeTick+=500;pump();expect("timeout retains outstanding owner",g_feedbackPulseActive,true);expect("timeout leaves no recurring work",g_gamepadSerialFeedbackStop.has_value(),false);expect("no blind timeout zero",zerosFor(occupant),0);
   pathReadable=true;fakeTick+=4000;sample(true);pump();expect("late same identity still receives owed zero",zerosFor(occupant),1);expect("late same identity retires owner",g_feedbackPulseActive,false);start();fakeTick+=18;pump();expect("fresh pulse after resolved debt stops",zerosFor(occupant),2);
  }else{
   pathReadable=true;
   if(recovery=="different")occupant=L"HID#physical-B";if(recovery=="virtual")occupant=L"HID#HIDMAESTRO_VID_045E_PID_0B00";
   if(recovery=="disconnect")connected=false;if(recovery=="provider")openAvailable=false;
   fakeTick+=5;sample(true);pump();expect("resolution reclaims retained owner",g_feedbackPulseActive,false);
   expect("only same identity may receive old zero",zerosFor(occupant),recovery=="same"?1:0);
  }
  finish("Unknown-"+recovery);
 }
 {
  reset();sample();start();pathReadable=false;sample(true);fakeTick+=18;pump();fakeTick+=499;sample(true);expect("repeated Unknown does not extend lifetime",g_gamepadSerialFeedbackStop?g_gamepadSerialFeedbackStop->due:0ULL,1518ULL);fakeTick++;pump();expect("budget expires into dormant ownership",g_feedbackPulseActive,true);expect("dormant has no recurring timer",g_gamepadSerialFeedbackStop.has_value(),false);finish("Unknown-budget-not-reset-by-repeated-probe");
 }
 {
  reset();sample();start();openAvailable=false;sample(true);fakeTick+=18;pump();expect("availability loss retires old owner",g_feedbackPulseActive,false);expect("compat input remains Physical",xinputPhysicalConnectedMask(),1u);auto fallback=start();expect("legacy pathless feedback remains available",fallback.ok,true);fakeTick+=18;pump();expect("fallback stop is independent from retired full-path proof",zerosFor(occupant),1);openAvailable=true;sample(true);start();fakeTick+=18;pump();expect("restored authority fresh pulse zero",zerosFor(occupant),2);finish("provider-availability-revokes-proof");
 }
 for(const std::string discovery:{"dirty","boundary"}){
  reset();openAvailable=false;sample();auto r=start();sample(discovery=="dirty",discovery=="boundary");fakeTick+=18;pump();
  expect("pathless startup feedback starts",r.ok,true);expect("pathless provider does not query paths",pathQueries,0);
  expect("pathless unchanged provider retains zero",zerosFor(occupant),1);expect("pathless pulse owner released",g_feedbackPulseActive,false);
  finish("legacy-unavailable-"+discovery);
 }
 for(const bool different:{false,true}){
  reset();sample();start();pathReadable=false;sample(true);fakeTick+=18;pump();fakeTick+=500;pump();
  expect("unknown owner retained past retry budget",g_feedbackPulseActive,true);
  for(int i=0;i<20;++i){fakeTick+=1000;sample(true);pump();}
  expect("long unknown never silently drops owner",g_feedbackPulseActive,true);
  expect("long unknown has no recurring deadline",g_gamepadSerialFeedbackStop.has_value(),false);expect("pending log is transition-only",pendingLog.size(),(size_t)2);
  expect("long unknown does not admit new pulse",nativePhysicalFeedbackStart(26,18,false).attempted,false);
  pathReadable=true;if(different)occupant=L"HID#physical-B";sample(true);pump();
  expect("identity event settles dormant ownership",g_feedbackPulseActive,false);
  expect("only proven same source receives old zero",zerosFor(occupant),different?0:1);
  finish(different?"dormant-debt-different-source":"dormant-debt-same-source");
 }
 for(const std::string discovery:{"dirty","boundary"}){
  reset();sample();expect("initial cached physical admitted",xinputPhysicalConnectedMask(),1u);
  occupant=L"HID#HIDMAESTRO_VID_045E_PID_0B00";sample(discovery=="dirty",discovery=="boundary");
  expect("reused virtual slot excluded from physical source",xinputPhysicalConnectedMask(),0u);
  occupant=L"HID#physical-B";sample(discovery=="dirty",discovery=="boundary");
  expect("reused physical source returns without edge",xinputPhysicalConnectedMask(),1u);
  finish("source-mask-replacement-"+discovery);
 }
 for(const bool persistent:{false,true}){
  reset();sample();start();zeroFailures=persistent?3:1;fakeTick+=18;pump();
  expect("failed zero retains ownership",g_feedbackPulseActive,true);
  expect("failed zero does not count as stop",zerosFor(occupant),0);
  const json row=pendingLog.empty()?json::object():pendingLog[0];
  expect("failed zero log has actual API code",row.value("writeError",0),5);
  expect("failed zero log distinguishes reason",row.value("reason",std::string{}),std::string("zero-write-failed"));
  expect("failed zero log does not claim motor observation",row.value("hardwareStopObserved",true),false);
  fakeTick+=500;pump();
  expect("retry failure is dormant, success clears",g_feedbackPulseActive,persistent);
  expect("successful retry alone proves zero",zerosFor(occupant),persistent?0:1);
  expect("zero-failure retry does not create recurring timer",g_gamepadSerialFeedbackStop.has_value(),false);
  if(persistent){zeroFailures=0;sample(true);pump();expect("existing source event retries dormant zero",zerosFor(occupant),1);}
  finish(persistent?"zero-write-failure-retains-debt":"zero-write-failure-recovers");
 }
 {
  reset();sample();start();throwPendingLog=true;pathReadable=false;sample(true);fakeTick+=18;pump();
  expect("logger failure cannot discard owed zero",g_feedbackPulseActive,true);fakeTick+=500;pump();
  pathReadable=true;sample(true);pump();expect("logger failure cannot prevent actual zero",zerosFor(occupant),1);
  expect("logger failure still retires owner after zero",g_feedbackPulseActive,false);finish("best-effort-log-never-controls-feedback");
 }
 for(const std::string force:{"edge","reprobe","disconnect"}){
  reset();sample();auto r=start();const auto before=g_feedbackSlotGeneration[0];
  if(force=="edge")g_xinputVirtualEndpointProbed=0;
  if(force=="reprobe")xinputSlotsRequestIdentityReprobe(1,"fixture");
  if(force=="disconnect"){connected=false;sample();connected=true;}
  sample(true);fakeTick+=18;pump();expect("forced probe generation invalidates",g_feedbackSlotGeneration[0]>before,true);expect("forced episode cannot use old zero",zerosFor(occupant),0);expect("forced owner reclaimed",g_feedbackPulseActive,false);start();fakeTick+=18;pump();expect("new forced episode owns own stop",zerosFor(occupant),1);finish("legacy-forced-"+force);
 }
 for(const std::string drift:{"epoch","power"}){
  reset();sample();start();inProbe=[&]{if(drift=="epoch")xinputSlotsRequestIdentityReprobe(1,"inflight");else ++fakePower;};sample(true);inProbe={};
  expect("probe drift requests fresh identity",(g_xinputSlotReprobeMask.load()&1u)!=0,true);expect("drift blocks new feedback",nativePhysicalFeedbackStart(26,18,false).attempted,false);fakeTick+=18;pump();expect("drift old zero suppressed",zerosFor(occupant),0);sample(true);start();fakeTick+=18;pump();expect("fresh proof recovers",zerosFor(occupant),1);finish("inflight-"+drift+"-change");
 }
 for(const bool swap:{false,true}){
  reset();sample();auto r=start();if(swap)occupant=L"HID#physical-B";
  std::promise<void> entered,release,finishStarted;auto releaseFuture=release.get_future().share();
  inProbe=[&]{entered.set_value();if(releaseFuture.wait_for(std::chrono::seconds(3))!=std::future_status::ready)throw std::runtime_error("probe timeout");};
  auto sampling=std::async(std::launch::async,[&]{sample(true);});if(entered.get_future().wait_for(std::chrono::seconds(3))!=std::future_status::ready)throw std::runtime_error("sampler not entered");
  auto stopping=std::async(std::launch::async,[&]{finishStarted.set_value();physicalFeedbackFinish(r.pulseGeneration);});finishStarted.get_future().wait();release.set_value();sampling.get();stopping.get();inProbe={};
  expect("serialized stop sees committed complete identity",zerosFor(occupant),swap?0:1);expect("concurrent owner reclaimed",g_feedbackPulseActive,false);finish(swap?"concurrent-probe-stop-new-physical":"concurrent-probe-stop-same");
 }
 {
  reset();sample();auto a=start();pathReadable=false;sample(true);fakeTick+=18;pump();pathReadable=true;occupant=L"HID#physical-B";sample(true);auto b=start();auto due=g_gamepadSerialFeedbackStop->due;
  gamepadSerialScheduleFeedbackStop(a.pulseGeneration,0);expect("stale notification keeps new generation",g_gamepadSerialFeedbackStop->generation,b.pulseGeneration);expect("stale notification keeps new due",g_gamepadSerialFeedbackStop->due,due);physicalFeedbackFinish(a.pulseGeneration);expect("stale finish cannot clear new owner",g_feedbackPulseActive,true);gamepadSerialScheduleFeedbackStop(b.pulseGeneration,40);expect("same owner cannot postpone zero",g_gamepadSerialFeedbackStop->due,due);fakeTick+=18;pump();expect("new pulse reaches zero",zerosFor(occupant),1);finish("late-source-notification-vs-new-stop-deadline");
 }
 {
  reset();sample();start();const auto q=pathQueries;const auto gen=g_feedbackSlotGeneration[0];for(int i=0;i<100;++i)sample();expect("stable tick has no path query",pathQueries,q);expect("stable tick has no generation churn",g_feedbackSlotGeneration[0],gen);fakeTick+=18;pump();expect("stable pulse stops",zerosFor(occupant),1);finish("stable-no-extra-poll");
 }
 for(const bool a1m:{false,true}){
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;if(a1m)g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawA1M";
  openAvailable=false;sample(true);const auto token=nativePhysicalFeedbackReady();expect("MSI ready bypasses XInput policy",token.backend==ymcc::touchhaptic::Backend::Msi,true);auto r=nativePhysicalFeedbackStart(26,18,true,&token);expect("MSI pulse succeeds",r.ok,true);expect("MSI original motor mapping",fixtureMsiMotor(),a1m?193:66);gamepadSerialScheduleFeedbackStop(r.pulseGeneration,18);fakeTick+=18;pump();expect("MSI zero intact",fixtureMsiZeros(),1);expect("MSI owner reclaimed",g_feedbackPulseActive,false);expect("no XInput output",writes.size(),size_t(0));finish(a1m?"MSI-A1M-DInput":"MSI-DInput");
 }
 {
  using namespace ymcc::input::identityRefresh;Slot slot;Probe p{true,false,false,false,L"HID#same",Kind::Physical,1,1,1,1};slot.observe(p);const auto proof=slot.capture(1,1);p.kind=Kind::Virtual;auto change=slot.observe(p);expect("classification belongs to full identity",change.advanceGeneration,true);expect("same path changed class retires proof",slot.stop(proof,1,1)==Stop::Retired,true);finish("full-key-includes-classification");
 }
#if FIXED_POLICY && defined(R108_MSI_RETURN_TAP)
 {
  reset();const auto eventBefore=g_msiOwnedReturnSourceEvent.load();sample(true);
  expect("common physical identity does not touch MSI cache",g_msiOwnedReturnSlotPaths[0].empty(),true);
  expect("common physical identity does not emit MSI event",g_msiOwnedReturnSourceEvent.load(),eventBefore);
  finish("R108-common-identity-observer-isolation");
 }
 {
  reset();returnTapMsi=true;sample(true);const auto event=g_msiOwnedReturnSourceEvent.load();const auto queries=pathQueries;
  expect("MSI path copied from existing probe",g_msiOwnedReturnSlotPaths[0]==occupant,true);
  for(int i=0;i<100;++i)sample();
  expect("MSI stable input adds no path query",pathQueries,queries);
  expect("MSI stable input adds no source event",g_msiOwnedReturnSourceEvent.load(),event);
  occupant=L"HID#physical-C";sample(true);
  expect("MSI changed occupant emits existing source edge",g_msiOwnedReturnSourceEvent.load()>event,true);
  finish("R108-MSI-cache-and-source-edge-without-extra-poll");
 }
#endif
#if FIXED_POLICY && defined(YMCC_UNIFIED_PHYSICAL_FEEDBACK)
 {
  reset();sample();const auto r=start();
  expect("actual XInput pulse keeps total capability",r.capabilityToken,unsigned(1));
  expect("actual XInput owner retains total binding",g_feedbackPulseTarget.unifiedBinding.token(),unsigned(1));
  const auto receipt=nativeFeedbackReceiptJson(r);
  expect("actual XInput receipt has total ID",receipt["unifiedCapabilityId"].get<std::string>(),std::string("controller.feedback.xinput.physical"));
  expect("manual pulse does not prove game forwarding",receipt["gameFeedbackForwardingProven"].get<bool>(),false);
  fakeTick+=18;pump();expect("total bound XInput pulse returns original zero",zerosFor(occupant),1);finish("total-XInput-feedback-consumer");
 }
 {
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;
  const auto token=nativePhysicalFeedbackReady();const auto r=nativePhysicalFeedbackStart(26,18,true,&token);
  expect("actual MSI pulse keeps total capability",r.capabilityToken,unsigned(2));
  expect("actual MSI owner retains total binding",g_feedbackPulseTarget.unifiedBinding.token(),unsigned(2));
  const auto receipt=nativeFeedbackReceiptJson(r);
  expect("actual MSI receipt has total ID",receipt["unifiedCapabilityId"].get<std::string>(),std::string("controller.feedback.msi-dinput.physical"));
  expect("actual MSI pulse uses current physical source",fixtureMsiWrites(),1);gamepadSerialScheduleFeedbackStop(r.pulseGeneration,18);fakeTick+=18;pump();expect("total bound MSI returns original zero",fixtureMsiZeros(),1);finish("total-MSI-feedback-consumer");
 }
 {
  reset();sample();const auto r=start();pathReadable=false;sample(true);fakeTick+=18;pump();
  expect("pending stop retains total capability",g_feedbackPulseTarget.unifiedBinding.token(),unsigned(1));
  expect("pending stop retains original owned resource",g_feedbackPulseActive,true);
  pathReadable=true;sample(true);pump();expect("total binding does not bypass original release proof",g_feedbackPulseActive,false);finish("total-feedback-pending-release");
 }
 {
  using namespace ymcc::capabilities::controller;
  expect("unknown feedback provider is refused",bindPhysicalFeedback(static_cast<PhysicalFeedbackKind>(9)).known(),false);
  const auto original=bindPhysicalFeedback(PhysicalFeedbackKind::XInput);auto copied=*original.provider;
  expect("copied provider cannot forge total binding",PhysicalFeedbackBinding{&copied}.known(),false);finish("total-feedback-provider-integrity");
 }
 {
  reset();sample();writeAllowed=false;const auto r=nativePhysicalFeedbackStart(26,18,false);
  expect("total registration does not grant hardware authority",r.attempted,false);expect("denied physical source leaves no receipt token",r.capabilityToken,unsigned(0));expect("denied write touches no actuator",writes.size(),size_t(0));finish("total-feedback-live-authority");
 }
#endif
#if FIXED_POLICY && defined(YMCC_UNIFIED_PHYSICAL_FEEDBACK)
 for(const unsigned length:{6u,32u,64u,96u}){
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;fixtureMsiReportLength=length;
  auto r=nativePhysicalFeedbackStart(26,18,false);expect("real MSI writer accepts selected report length",r.ok,true);expect("real MSI writer has bound receipt",r.capabilityToken,unsigned(2));expect("real MSI WriteFile gets exact report length",fixtureMsiLastBuffer.size(),size_t(length));expect("real MSI frame has HC report id",(unsigned)fixtureMsiLastBuffer[0],5u);expect("real MSI frame has command",(unsigned)fixtureMsiLastBuffer[1],1u);expect("real MSI small and large mapping",(unsigned)fixtureMsiLastBuffer[4]*256u+fixtureMsiLastBuffer[5],66u*257u);
  bool tail=true;for(size_t i=6;i<fixtureMsiLastBuffer.size();++i)tail&=fixtureMsiLastBuffer[i]==0;expect("real MSI frame tail remains zero",tail,true);
  gamepadSerialScheduleFeedbackStop(r.pulseGeneration,18);fakeTick+=18;pump();expect("real MSI zero uses original writer",fixtureMsiZeros(),1);expect("real MSI resource is reused for zero",fixtureMsiOpens,1u);expect("real MSI source cache built once",g_msiFeedbackIdentityBuilds.load(),1ULL);finish("actual-MSI-writer-length-"+std::to_string(length));
 }
 for(const std::string reason:{"vendor","product","usage","length","caps"}){
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;
  if(reason=="vendor")fixtureMsiVid=1;if(reason=="product")fixtureMsiPid=1;if(reason=="usage")fixtureMsiUsagePage=0xffa0;if(reason=="length")fixtureMsiReportLength=5;if(reason=="caps")fixtureMsiCaps=false;
  const auto r=nativePhysicalFeedbackStart(26,18,false);expect("real MSI source rejection never attempts write",r.attempted,false);expect("real MSI source rejection has no WriteFile",fixtureMsiSdkReports.size(),size_t(0));expect("real MSI source rejection closes owned open",fixtureMsiCloses,1u);finish("actual-MSI-open-refusal-"+reason);
 }
 for(const auto mode:{FixtureMsiIo::Short,FixtureMsiIo::Failed,FixtureMsiIo::Timeout}){
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;fixtureMsiIo=mode;
  const auto r=nativePhysicalFeedbackStart(26,18,false);expect("real MSI write failure is not success",r.ok,false);expect("real MSI failed resource is retired",g_msiFeedbackLease.resource()==nullptr,true);if(mode==FixtureMsiIo::Timeout)expect("real MSI timeout cancels owned overlapped request",fixtureMsiCancels,1u);finish("actual-MSI-write-failure-"+std::to_string((int)mode));
 }
 {
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;fixtureMsiIo=FixtureMsiIo::Incomplete;
  const auto r=nativePhysicalFeedbackStart(26,18,false);expect("pending MSI write retains one resource",g_msiFeedbackLease.resource()!=nullptr,true);expect("pending MSI write fails safely",r.ok,false);const auto opens=fixtureMsiOpens;g_msiFeedbackModeGeneration=2;
  const auto blocked=nativePhysicalFeedbackStart(26,18,false);expect("unreaped MSI request blocks another resource",fixtureMsiOpens,opens);expect("unreaped MSI request is pending",blocked.reason==ymcc::touchhaptic::Reason::IoPending,true);
  fixtureMsiIo=FixtureMsiIo::Sync;fakeTick+=1000;const auto recovered=nativePhysicalFeedbackStart(26,18,false);expect("reaped MSI owner permits fresh write",recovered.ok,true);expect("reaped MSI owner opens exactly one successor",fixtureMsiOpens,opens+1);gamepadSerialScheduleFeedbackStop(recovered.pulseGeneration,18);fakeTick+=18;pump();finish("actual-MSI-pending-resource-recovery");
 }
#endif
#if FIXED_POLICY && defined(YMCC_UNIFIED_PHYSICAL_FEEDBACK)
 {
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();MsiFeedbackLeaseOps::fixtureWrites=nullptr;
  const auto r=msiDinputFeedbackWrite(17,93,false,"asymmetric-fixture");
  expect("real MSI asymmetric report is accepted",r.ok,true);
  expect("real MSI small motor preserves index four",(unsigned)fixtureMsiLastBuffer[4],17u);
  expect("real MSI large motor preserves index five",(unsigned)fixtureMsiLastBuffer[5],93u);finish("actual-MSI-asymmetric-original-frame");
 }
 {
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;
  fixtureMsiBeforeOpen=[](){writeAllowed=false;};const auto r=nativePhysicalFeedbackStart(26,18,false);
  expect("real MSI final authority is rechecked after open",r.attempted,false);
  expect("real MSI changed authority reaches no WriteFile",fixtureMsiSdkReports.size(),size_t(0));
  expect("real MSI changed authority retires owned resource",g_msiFeedbackLease.resource()==nullptr,true);finish("actual-MSI-final-authority-refusal");
 }
#endif
#if FIXED_POLICY && defined(YMCC_UNIFIED_PHYSICAL_FEEDBACK)
 for(const auto failure:{FixtureMsiIo::Short,FixtureMsiIo::Failed,FixtureMsiIo::Timeout}){
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;
  const auto pulse=nativePhysicalFeedbackStart(26,18,false);expect("MSI zero regression starts real physical writer",pulse.ok,true);
  fixtureMsiIo=failure;gamepadSerialScheduleFeedbackStop(pulse.pulseGeneration,18);fakeTick+=18;pump();
  expect("failed MSI zero retains outstanding physical feedback owner",g_feedbackPulseActive,true);
  expect("failed MSI zero schedules bounded retry",g_gamepadSerialFeedbackStop.has_value(),true);
  expect("failed MSI zero is not API accepted",pendingLog.empty()?true:pendingLog.back()["zeroApiAccepted"].get<bool>(),false);
  fixtureMsiIo=FixtureMsiIo::Sync;fakeTick+=500;pump();
  expect("successful MSI zero alone settles owner",g_feedbackPulseActive,false);
  expect("successful MSI retry has zero API receipt",!pendingLog.empty()&&pendingLog.back()["zeroApiAccepted"].get<bool>(),true);
  expect("successful MSI retry does not claim physical observation",!pendingLog.empty()&&pendingLog.back()["hardwareStopObserved"].get<bool>(),false);
  finish("actual-MSI-zero-failure-retry-"+std::to_string((int)failure));
 }
 {
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;
  const auto pulse=nativePhysicalFeedbackStart(26,18,false);fixtureMsiIo=FixtureMsiIo::Failed;gamepadSerialScheduleFeedbackStop(pulse.pulseGeneration,18);fakeTick+=18;pump();fakeTick+=500;pump();
  expect("exhausted MSI stop budget retains one owner",g_feedbackPulseActive,true);
  expect("exhausted MSI stop budget leaves no recurring timer",g_gamepadSerialFeedbackStop.has_value(),false);
  const auto writesBefore=fixtureMsiSdkReports.size();fakeTick+=100000;pump();expect("dormant MSI debt does no background writes",fixtureMsiSdkReports.size(),writesBefore);
  fixtureMsiIo=FixtureMsiIo::Sync;expect("late MSI source notification wakes dormant zero",msiDinputFeedbackWakePendingStop(),true);pump();
  expect("late MSI same-owner recovery accepts actual zero",!pendingLog.empty()&&pendingLog.back()["zeroApiAccepted"].get<bool>(),true);
  expect("late MSI zero reclaims retained owner",g_feedbackPulseActive,false);finish("actual-MSI-bounded-stop-debt-and-source-wake");
 }
 {
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;
  const auto pulse=nativePhysicalFeedbackStart(26,18,false);fixtureMsiIo=FixtureMsiIo::Failed;gamepadSerialScheduleFeedbackStop(pulse.pulseGeneration,18);fakeTick+=18;pump();fakeTick+=500;pump();g_gamepadSerialStopping=true;
  expect("unavailable MSI scheduler reports failure",msiDinputFeedbackWakePendingStop(),false);expect("unavailable MSI scheduler does not clear debt",g_feedbackPulseActive,true);
  g_gamepadSerialStopping=false;fixtureMsiIo=FixtureMsiIo::Sync;expect("available MSI scheduler accepts existing debt",msiDinputFeedbackWakePendingStop(),true);pump();expect("MSI debt settles after real retry",g_feedbackPulseActive,false);finish("actual-MSI-stop-scheduler-unavailable");
 }
 {
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;
  const auto pulse=nativePhysicalFeedbackStart(26,18,false);fixtureMsiIo=FixtureMsiIo::Failed;gamepadSerialScheduleFeedbackStop(pulse.pulseGeneration,18);fakeTick+=18;pump();fakeTick+=500;pump();const auto before=fixtureMsiSdkReports.size();
  g_msiMainSource.interfacePath="HID#other-MSI-instance";g_msiMainSource.generation=8;g_msiFeedbackSourceRecord.reset();g_msiFeedbackSourceGeneration=8;fixtureMsiIo=FixtureMsiIo::Sync;
  expect("MSI replacement source wakes old debt check",msiDinputFeedbackWakePendingStop(),true);pump();
  expect("MSI old zero never writes a replacement source",fixtureMsiSdkReports.size(),before);expect("MSI replaced owner is retired",g_feedbackPulseActive,false);
  expect("MSI retirement never forges zero API acceptance",!pendingLog.empty()&&pendingLog.back()["zeroApiAccepted"].get<bool>(),false);finish("actual-MSI-replacement-source-cannot-receive-old-zero");
 }
 {
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();fixturePublishMsiSource();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;MsiFeedbackLeaseOps::fixtureWrites=nullptr;
  const auto oldPulse=nativePhysicalFeedbackStart(26,18,false);fixtureMsiIo=FixtureMsiIo::Failed;gamepadSerialScheduleFeedbackStop(oldPulse.pulseGeneration,18);fakeTick+=18;pump();fixtureMsiIo=FixtureMsiIo::Sync;
  fakeTick+=250; // Respect the existing ordinary-write retry delay; safety zero alone bypasses it.
  const auto fresh=nativePhysicalFeedbackStart(26,18,false);expect("MSI newer pulse admitted after existing backoff",fresh.ok,true);gamepadSerialScheduleFeedbackStop(fresh.pulseGeneration,18);physicalFeedbackFinish(oldPulse.pulseGeneration);
  expect("MSI old stop cannot clear newer pulse",g_feedbackPulseActive,true);expect("MSI newer pulse owns its generation",fresh.pulseGeneration>oldPulse.pulseGeneration,true);
  fakeTick+=18;pump();expect("MSI newer pulse performs its own zero",g_feedbackPulseActive,false);finish("actual-MSI-old-stop-cannot-steal-newer-pulse");
 }
#endif
#if FIXED_POLICY
 // New source/power episode initialization is event driven. All of these calls
 // execute the extracted production physical writer, against the same bounded
 // in-memory final API used by the existing source/stop lifecycle tests.
 {
  reset();fakePower=21001;sample();const auto source=nativePhysicalFeedbackReady();
  const auto first=nativePhysicalFeedbackStart(40,18,true,&source);
  expect("fresh output episode establishes zero before nonzero",writes.size()==2&&writes[0]["left"]==0&&writes[1]["left"]==26214,true);
  expect("baseline API acceptance recorded",first.outputBaselineAttempted&&first.outputBaselineAccepted&&first.ok,true);
  physicalFeedbackFinish(first.pulseGeneration);writes=json::array();
  const auto repeat=nativePhysicalFeedbackStart(40,18,true,&source);
  expect("steady motion adds no extra baseline write",writes.size()==1&&writes[0]["left"]==26214&&!repeat.outputBaselineAttempted,true);
  physicalFeedbackFinish(repeat.pulseGeneration);finish("touch-output-episode-baseline-is-not-per-move");
 }
 {
  reset();fakePower=21002;sample();const auto before=nativePhysicalFeedbackReady();
  const auto prior=nativePhysicalFeedbackStart(60,18,true,&before);physicalFeedbackFinish(prior.pulseGeneration);
  ++fakePower;sample(true);writes=json::array();outputLatchNeedsZero=true;
  const auto stale=nativePhysicalFeedbackStart(60,18,true,&before);
  expect("old pre-sleep ticket never primes replacement power",stale.reason==ymcc::touchhaptic::Reason::PowerTransition&&writes.empty(),true);
  XINPUT_VIBRATION cached{39321,39321};const auto apiOnly=fakeSetState(0,&cached);
  expect("fault reproduces API success without motor output",apiOnly==ERROR_SUCCESS&&motors[occupant]==0&&outputLatchNeedsZero,true);
  writes=json::array();const auto after=nativePhysicalFeedbackReady();
  const auto resumed=nativePhysicalFeedbackStart(60,18,true,&after);
  expect("fresh post-resume pulse clears stale output latch",resumed.ok&&resumed.outputBaselineAccepted&&motors[occupant]==39321&&!outputLatchNeedsZero,true);
  expect("post-resume sequence is zero then selected strength",writes.size()==2&&writes[0]["left"]==0&&writes[1]["left"]==39321,true);
  physicalFeedbackFinish(resumed.pulseGeneration);expect("resumed pulse retains generation-bound zero stop",motors[occupant],0);
  finish("touch-output-resume-api-success-is-not-motor-proof");
 }
 {
  reset();fakePower=21004;sample();const auto source=nativePhysicalFeedbackReady();zeroFailures=1;
  const auto failed=nativePhysicalFeedbackStart(20,18,true,&source);
  expect("failed baseline prevents nonzero write",!failed.ok&&failed.outputBaselineAttempted&&!failed.outputBaselineAccepted&&failed.error==5&&writes.empty(),true);
  const auto retry=nativePhysicalFeedbackStart(20,18,true,&source);
  expect("failed baseline is retried on next real touch",retry.ok&&retry.outputBaselineAccepted&&writes.size()==2&&writes[0]["left"]==0&&writes[1]["left"]==13107,true);
  physicalFeedbackFinish(retry.pulseGeneration);finish("touch-output-baseline-failure-is-retryable-without-timer");
 }
 {
  reset();fakePower=21005;sample();const auto source=nativePhysicalFeedbackReady();nonzeroFailures=1;
  const auto failed=nativePhysicalFeedbackStart(40,18,true,&source);
  expect("nonzero rejection remains a failed pulse",!failed.ok&&failed.outputBaselineAccepted&&failed.error==5,true);
  physicalFeedbackFinish(failed.pulseGeneration);writes=json::array();
  const auto retry=nativePhysicalFeedbackStart(40,18,true,&source);
  expect("only complete accepted pulse commits baseline",retry.ok&&retry.outputBaselineAttempted&&writes.size()==2&&writes[0]["left"]==0&&writes[1]["left"]==26214,true);
  physicalFeedbackFinish(retry.pulseGeneration);finish("touch-output-start-failure-does-not-commit-baseline");
 }
 {
  reset();fakePower=21006;sample();const auto source=nativePhysicalFeedbackReady();afterFeedbackWrite=[]{++fakePower;};
  const auto interrupted=nativePhysicalFeedbackStart(40,18,true,&source);
  expect("sleep during baseline never emits nonzero",!interrupted.ok&&interrupted.reason==ymcc::touchhaptic::Reason::PowerTransition&&writes.size()==1&&writes[0]["left"]==0,true);
  finish("touch-output-power-change-during-baseline-is-rechecked");
 }
 {
  reset();fakePower=21007;sample();const auto source=nativePhysicalFeedbackReady();afterFeedbackWrite=[]{++g_xinputTopologyEpoch;};
  const auto interrupted=nativePhysicalFeedbackStart(40,18,true,&source);
  expect("topology change during baseline never emits nonzero",!interrupted.ok&&interrupted.reason==ymcc::touchhaptic::Reason::SourceChanged&&writes.size()==1&&writes[0]["left"]==0,true);
  finish("touch-output-topology-change-during-baseline-is-rechecked");
 }
 {
  reset();fakePower=21008;sample();const auto source=nativePhysicalFeedbackReady();g_xinputVirtualEndpointMask=1;
  const auto denied=nativePhysicalFeedbackStart(40,18,true,&source);
  expect("virtual endpoint never receives baseline or pulse",!denied.ok&&!denied.outputBaselineAttempted&&writes.empty(),true);
  g_xinputVirtualEndpointMask=0;writeAllowed=false;
  const auto suspended=nativePhysicalFeedbackStart(40,18,true,&source);
  expect("suspend gate blocks even the baseline zero",!suspended.ok&&!suspended.outputBaselineAttempted&&writes.empty(),true);
  finish("touch-output-baseline-retains-physical-and-power-admission");
 }
 {
  reset();fakePower=21009;sample();const auto source=nativePhysicalFeedbackReady();const auto manual=start();writes=json::array();
  const auto denied=nativePhysicalFeedbackStart(40,18,true,&source);
  expect("first touch cannot prime through existing manual owner",!denied.ok&&denied.reason==ymcc::touchhaptic::Reason::ManualPulseActive&&writes.empty(),true);
  physicalFeedbackFinish(manual.pulseGeneration);finish("touch-output-baseline-preserves-manual-owner");
 }
 {
  reset();fakePower=21010;sample();
  for(const auto strength:{20,40,60}){
   const auto source=nativePhysicalFeedbackReady();const auto pulse=nativePhysicalFeedbackStart(strength,18,true,&source);
   expect((std::string("amplified requested motor value ")+std::to_string(strength)).c_str(),pulse.ok&&motors[occupant]==65535*strength/100,true);
   physicalFeedbackFinish(pulse.pulseGeneration);expect("selected tier still stops",motors[occupant],0);
  }
  finish("touch-output-amplified-three-tiers-retain-stop");
 }
#endif
 int failures=0,total=0,bad=0;for(const auto& c:cases){failures+=!c["passed"].get<bool>();for(const auto& e:c["checks"]){++total;bad+=!e["passed"].get<bool>();}}
 std::cout<<json{{"caseCount",cases.size()},{"checks",total},{"failedCases",failures},{"failedChecks",bad},{"cases",cases},{"actualStartStopFinishAndScheduleExtracted",true},{"deviceHostSdkExecuted",false}}.dump(2)<<std::endl;return failures?1:0;
}
