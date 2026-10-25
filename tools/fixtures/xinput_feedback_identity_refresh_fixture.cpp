// Pure local harness. Production functions/blocks are inserted verbatim by the
// driver; only XInput/MSI, time, scheduler wake and source observations are fake.
#include "json.hpp"
#include "touch_haptic_dispatch.h"
#include "xinput_identity_refresh_policy.h"
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
static void appendNativeLifecycleLog(const char*,const json&){}
static std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}
static bool openAvailable=true,pathReadable=true,connected=true,writeAllowed=true;
static std::wstring occupant=L"HID#physical-A";
static int pathQueries=0,availabilityQueries=0;
static std::function<void()> inProbe;
static bool ymccOpenXInputAvailable(){++availabilityQueries;return openAvailable;}
static std::wstring ymccOpenXInputGetDevicePath(DWORD){++pathQueries;auto result=pathReadable?occupant:L"";if(inProbe)inProbe();return result;}
@@IDENTITY_ENUM@@
@@METHODS@@
struct HidHideMsiMainSourceRecord {unsigned long long generation=7;};
static std::shared_ptr<const HidHideMsiMainSourceRecord> msiSource;
static auto msiDinputFeedbackSourceSnapshot(){return msiSource;}
static std::atomic<unsigned long long> g_msiFeedbackModeGeneration{1},g_msiFeedbackSourceGeneration{0},g_msiFeedbackSamplePower{1},g_msiFeedbackSampleMode{1},g_msiFeedbackSampleTopology{1};
static std::atomic<bool> g_msiDinputSourceValid{false};
static struct {std::string deviceClass="ROG";} g_machineIdentity;
static bool hardwareWriteAllowed(){return writeAllowed;}
static int msiMotor=0,msiZeros=0,msiWrites=0;
struct MsiWrite {bool ok=true,attempted=true,written=true;DWORD lastError=0;ymcc::touchhaptic::Reason reason=ymcc::touchhaptic::Reason::None;};
static MsiWrite msiDinputFeedbackWrite(BYTE left,BYTE right,bool,const char*,const HidHideMsiMainSourceRecord*,bool,const ymcc::touchhaptic::SourceToken*){++msiWrites;msiMotor=right;if(!left&&!right)++msiZeros;return {};}
static std::mutex g_feedbackPulseMx,g_gamepadReadMx,g_gamepadSerialMx;
static std::condition_variable g_gamepadSerialCv;
static bool g_gamepadSerialStopping=false;
struct FeedbackStopDeadline {unsigned long long generation=0,due=0;};
static std::optional<FeedbackStopDeadline> g_gamepadSerialFeedbackStop;
static void gamepadSerialStartLocked(){}
static bool gamepadSerialScheduleFeedbackStop(unsigned long long,int);
@@POLICY_HELPER@@
@@TARGET@@
static PhysicalFeedbackTarget g_feedbackPulseTarget;
static bool g_feedbackPulseActive=false,g_feedbackPulseTouch=false;
static unsigned long long g_rogVibratePulseGen=0;
static std::map<std::wstring,int> motors;
static json writes=json::array();
static DWORD fakeSetState(DWORD slot,XINPUT_VIBRATION* v){motors[occupant]=v->wRightMotorSpeed;writes.push_back({{"slot",slot},{"occupant",W2U(occupant)},{"left",v->wLeftMotorSpeed},{"right",v->wRightMotorSpeed}});return ERROR_SUCCESS;}
static auto g_feedbackSetState=fakeSetState;
@@CURRENT@@
@@STOP@@
@@FINISH@@
@@READY@@
@@START@@
@@SCHEDULE@@
static void sample(bool dirtyDiscovery=false,bool boundaryFullScan=false){
 std::lock_guard<std::mutex> lock(g_gamepadReadMx);
 const auto probeNow=GetTickCount64();
 if(connected){
  g_xinputConnectedMask=1;const DWORD i=0;
  @@CLASSIFICATION@@
 }else{
  g_xinputConnectedMask=0;g_xinputVirtualEndpointProbed=0;g_xinputSlotUnknownMask=0;g_xinputSlotIdentityResolvedEpoch[0]=0;
#if FIXED_POLICY
  g_xinputFeedbackIdentity[0].disconnect();
  if(const auto pending=g_xinputDeferredFeedbackStop.pendingFor(0))(void)gamepadSerialScheduleFeedbackStop(pending,0);
#endif
 }
}
static bool pump(){std::optional<FeedbackStopDeadline> stop;{std::lock_guard<std::mutex> l(g_gamepadSerialMx);if(g_gamepadSerialFeedbackStop&&g_gamepadSerialFeedbackStop->due<=GetTickCount64()){stop=g_gamepadSerialFeedbackStop;g_gamepadSerialFeedbackStop.reset();}}if(stop)physicalFeedbackFinish(stop->generation);return stop.has_value();}
static void reset(){
 fakeTick=1000;fakePower=1;g_xinputSlotIdentityEpoch=1;g_xinputTopologyEpoch=1;g_xinputDiscoveryDirty=false;g_xinputSlotReprobeMask=0;g_exitRequested=false;
 g_xinputConnectedMask=1;g_xinputVirtualEndpointMask=0;g_xinputVirtualEndpointProbed=0;g_xinputSlotUnknownMask=0;g_xinputAdmittedMask=1;
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
 msiSource.reset();g_msiFeedbackSourceGeneration=0;g_msiFeedbackModeGeneration=1;g_msiFeedbackSamplePower=1;g_msiFeedbackSampleMode=1;g_msiFeedbackSampleTopology=1;g_msiDinputSourceValid=false;msiMotor=msiZeros=msiWrites=0;g_machineIdentity.deviceClass="ROG";
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
   fakeTick+=500;pump();expect("timeout reclaims owner",g_feedbackPulseActive,false);expect("timeout leaves no recurring work",g_gamepadSerialFeedbackStop.has_value(),false);expect("no blind timeout zero",zerosFor(occupant),0);
   pathReadable=true;sample(true);start();fakeTick+=18;pump();expect("fresh pulse after timeout stops",zerosFor(occupant),1);
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
  reset();sample();start();pathReadable=false;sample(true);fakeTick+=18;pump();fakeTick+=499;sample(true);expect("repeated Unknown does not extend lifetime",g_gamepadSerialFeedbackStop?g_gamepadSerialFeedbackStop->due:0ULL,1518ULL);fakeTick++;pump();expect("budget expires once",g_feedbackPulseActive,false);finish("Unknown-budget-not-reset-by-repeated-probe");
 }
 {
  reset();sample();start();openAvailable=false;sample(true);fakeTick+=18;pump();expect("availability loss retires old owner",g_feedbackPulseActive,false);expect("compat input remains Physical",xinputPhysicalConnectedMask(),1u);expect("unverified new feedback denied",nativePhysicalFeedbackStart(26,18,false).attempted,false);openAvailable=true;sample(true);start();fakeTick+=18;pump();expect("restored authority fresh pulse zero",zerosFor(occupant),1);finish("provider-availability-revokes-proof");
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
  reset();msiSource=std::make_shared<HidHideMsiMainSourceRecord>();g_msiFeedbackSourceGeneration=7;g_msiDinputSourceValid=true;if(a1m)g_machineIdentity.deviceClass="HandheldCompanion.Devices.ClawA1M";
  openAvailable=false;sample(true);const auto token=nativePhysicalFeedbackReady();expect("MSI ready bypasses XInput policy",token.backend==ymcc::touchhaptic::Backend::Msi,true);auto r=nativePhysicalFeedbackStart(26,18,true,&token);expect("MSI pulse succeeds",r.ok,true);expect("MSI original motor mapping",msiMotor,a1m?193:66);gamepadSerialScheduleFeedbackStop(r.pulseGeneration,18);fakeTick+=18;pump();expect("MSI zero intact",msiZeros,1);expect("MSI owner reclaimed",g_feedbackPulseActive,false);expect("no XInput output",writes.size(),size_t(0));finish(a1m?"MSI-A1M-DInput":"MSI-DInput");
 }
 {
  using namespace ymcc::input::identityRefresh;Slot slot;Probe p{true,false,false,false,L"HID#same",Kind::Physical,1,1,1,1};slot.observe(p);const auto proof=slot.capture(1,1);p.kind=Kind::Virtual;auto change=slot.observe(p);expect("classification belongs to full identity",change.advanceGeneration,true);expect("same path changed class retires proof",slot.stop(proof,1,1)==Stop::Retired,true);finish("full-key-includes-classification");
 }
 int failures=0,total=0,bad=0;for(const auto& c:cases){failures+=!c["passed"].get<bool>();for(const auto& e:c["checks"]){++total;bad+=!e["passed"].get<bool>();}}
 std::cout<<json{{"caseCount",cases.size()},{"checks",total},{"failedCases",failures},{"failedChecks",bad},{"cases",cases},{"actualStartStopFinishAndScheduleExtracted",true},{"deviceHostSdkExecuted",false}}.dump(2)<<std::endl;return failures?1:0;
}
