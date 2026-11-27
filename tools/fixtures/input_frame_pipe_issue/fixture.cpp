#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>
#include "windows.h"
#include "json.hpp"
#include "input_frame_submission.h"
#include "touch_haptic_dispatch.h"
#include "game_feedback_receipt.h"
#include "windows_motion_submission.h"
#include "legion_motion_source.h"
#include "legion_motion_message_clock.h"
#include "HcControllerMotionCapabilityCatalog.h"
#include "input_host_pipe_transport.h"
#include "input_host_shared_power.h"
#include "input_host_compact_frame_reply.h"
#include "sleep_power_remote_contract.h"
#include "msi_virtual_report_diagnostic.h"
#include "virtual_recovery_intent.h"
#include "virtual_start_protection.h"
// Explicit policy facts; no installed application or output device is used.
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound{true};
using json=nlohmann::json;
static ymcc::inputHost::VirtualRecoveryIntent g_inputHostRecoveryIntent;
static ymcc::VirtualStartProtection g_virtualStartProtection;
static bool g_inputHostHello=true,g_inputHostReleaseQuarantined=false;
struct XINPUT_GAMEPAD{std::uint16_t wButtons=0;BYTE bLeftTrigger=0,bRightTrigger=0;short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
namespace ymcc::screenpads{struct Snapshot{bool present=false;};inline json snapshotJson(const Snapshot&){return json::object();}}
@@SNAPSHOT@@
static thread_local const ymcc::legion_motion::Sample* t_legionMotionSubmission=nullptr;
static thread_local const ymcc::windows_motion::Stamp* t_windowsMotionSubmission=nullptr;
// Serial feedback remains an inert terminal outside frame/power acceptance.
struct InputFrameSubmission;
static void nativeSubmitGameFeedbackFrame(const InputFrameSubmission&,bool);
static void gamepadSerialRevokeGameFeedback() {}
static ymcc::inputHost::sharedPower::Cache g_inputHostInputPowerCache,g_inputHostLifecyclePowerCache;
static ymcc::sleeppower::RevocationMirrors g_sharedPowerRevocations;
@@REQUEST_TLS@@
@@SCOPE@@
static void nativeSubmitGameFeedbackFrame(const InputFrameSubmission&,bool) {}
// The original frame clock is also the fake Win32 deadline clock (windows.h).
static thread_local std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0;
static bool g_inputHostPrepareMayHaveReachedHost=false;static unsigned targetCreateEntries=0;
// Non-frame target creation is outside this transport fixture; it must never succeed or run.
static bool inputHostTargetReturnBeforeCreateLocked(const char*,std::uint64_t){++targetCreateEntries;throw std::logic_error("target-create-outside-frame-fixture");}
// This frame-only fixture owns no deferred lifecycle debt. The actual private
// guard/refresh consumer is covered by the original full-TU late-hide suite.
// Never synthesize permission if a non-frame caller reaches this excluded port.
struct InputHostDeferredPhysicalHide;
static thread_local const InputHostDeferredPhysicalHide* t_inputHostDeferredPhysicalHideWrite=nullptr;
static bool inputHostDeferredPhysicalHideWriteCurrent(){throw std::logic_error("deferred-lifecycle-outside-frame-fixture");}
@@COMMAND_DEADLINES@@
static unsigned long long power=1,g_inputHostEpoch=1;static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{0},g_msiMainSourceRevision{7},g_msiFeedbackModeGeneration{1};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0},g_xinputTopologyEpoch{1};static std::atomic<unsigned> g_xinputSlotIdentityEpoch{1};
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false};
static bool writerAllowed=true,exitFence=false,isMsi=false;
static HANDLE g_inputHostProcess=nullptr,g_inputHostServerPipe=nullptr;
static std::string g_inputHostRunId="fixture-same-run";
static std::string g_inputHostStartupNonce="fixture-same-nonce";
static std::string g_inputHostFrameReplyBinding;
static bool g_inputHostCompactFrameRequest=false;
namespace ymcc::sleeppower::remote {struct NativeRemoteTransport{};struct NativeProvider{};}
enum class FixtureOffer { Available, NoTransport, Rejected };
static FixtureOffer fixtureOffer=FixtureOffer::Available;
static unsigned fixtureOfferCalls=0,fixtureForgottenTargets=0;
static ymcc::sleeppower::remote::NativeProvider& sharedNativeRemotePowerProvider(){
 static ymcc::sleeppower::remote::NativeProvider provider;return provider;
}
static ymcc::sleeppower::remote::NativeRemoteTransport* sharedNativeRemotePowerTransport(){
 static ymcc::sleeppower::remote::NativeRemoteTransport port;
 return fixtureOffer==FixtureOffer::NoTransport?nullptr:&port;
}
// Provider/importer and authenticated wire bytes are outside this fragment fixture.
namespace ymcc::inputHost::sharedPower {
struct RemoteOffers {
 struct Ticket{};
 bool prepare(ymcc::sleeppower::remote::NativeProvider&,ymcc::sleeppower::remote::NativeRemoteTransport&,
     const std::shared_ptr<ymcc::inputHost::sharedPower::Scope>& source,HANDLE consumer,
     const std::string& run,std::uint64_t epoch,const std::string& nonce,
     ymcc::sleeppower::remote::Purpose purpose,const std::string*& header,Ticket&) const noexcept {
  ++fixtureOfferCalls;header=nullptr;
  return fixtureOffer==FixtureOffer::Available&&source&&source->current()&&consumer&&consumer==g_inputHostProcess&&
   !run.empty()&&run==g_inputHostRunId&&epoch&&epoch==g_inputHostEpoch&&!nonce.empty()&&nonce==g_inputHostStartupNonce&&
   ymcc::sleeppower::remote::inputPurpose(purpose);
 }
};
}
static ymcc::inputHost::sharedPower::RemoteOffers g_inputHostRemotePowerOffers;
static void inputHostSwitchVerificationForgetAcceptedLocked() noexcept {++fixtureForgottenTargets;}
static struct {void revoke() noexcept {}} g_inputHostGameMotorInbox;
static std::atomic<std::uint64_t> g_inputHostGameMotorBindingGeneration{0};
static bool g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostTransportFault=false,g_inputHostPipeConnected=false;
static std::atomic<bool> g_msiDinputRouteDinputArmed{false},g_msiDinputRouteConfirmed{true},g_msiDinputSourceValid{true};
static struct{std::atomic<bool> enabled{true},mainInterfaceMatched{true};std::atomic<unsigned> productId{0x1902};}g_msiDinput;
static constexpr struct{unsigned pidDInput=0x1902;}kMsiClawCapability;
static unsigned long long currentPowerGeneration(){return power;}static unsigned long long inputHostEpochToken(){return g_inputHostEpoch;}
static bool msiClawKnownBoardAuthority(){return isMsi;}static bool sgVirtualWriterAllowedNow(){return writerAllowed;}static bool inputHostExitFenceRaised(){return exitFence;}
// Inert source-generation/lifecycle inputs for the verbatim production motion guard.
static std::atomic<unsigned long long> g_hidResolveCacheGeneration{1},g_legionMotionCaptureEpoch{1},g_legionMotionEpochStartedAt{1};
static std::atomic<bool> g_legionRawInputSubscribed{true},g_inputCaptureStop{false};
enum class PowerLifecycle{Ready=0,Suspending=1,Suspended=2,Resuming=3,Stopped=4};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned selectedMotionSensor=0;static unsigned legionMotionSelectedSensorIndex() noexcept{return selectedMotionSensor;}
// Machine identity is an inert input; the model cache and final guard are production code.
enum class YmccFamily {Unknown,LenovoLegionGo};
static struct{YmccFamily family=YmccFamily::LenovoLegionGo;std::string deviceClass="HandheldCompanion.Devices.LegionGoTablet";}g_machineIdentity;
// One controlled Reader input for actual public Scope/Registration/helper code.
// This is not acceptance of Native's producer or notification callback wiring.
static std::uint64_t fixturePublication=1,fixtureNotification=1,fixtureSuspendSerial=1;
static bool fixtureNotificationPending=false,fixtureKeepTarget=true;
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* out,std::uint32_t size){
 if(!out||size!=sizeof(*out))return 0;
 *out={};out->generation=power;out->publicationRevision=fixturePublication;
 out->observedNotificationSequence=fixtureNotification;out->suspendSerial=fixtureSuspendSerial;
 out->phase=static_cast<unsigned>(g_powerLifecycle.load());
 out->flags=ymcc::sleeppower::kKnown|(out->phase==0?ymcc::sleeppower::kHardwareAllowed:0u)|
  (fixtureNotificationPending?ymcc::sleeppower::kNotificationPending:0u);return 1;
}
static bool sgShouldKeepInputDuringStandby(){return fixtureKeepTarget;}
static thread_local bool t_inputHostOneXStartWrite=false;
static bool inputHostOneXStartCurrent(){throw std::logic_error("excluded OneX current leaf");}
static bool inputHostOneXStartMayIssue(){throw std::logic_error("excluded OneX issue leaf");}
static std::uint64_t inputHostOneXStartDeadline(){throw std::logic_error("excluded OneX deadline leaf");}
static bool t_inputHostDeferredPhysicalHideRefreshIssueActive=false;
static bool inputHostDeferredPhysicalHideReserveRefreshIssue(){throw std::logic_error("excluded deferred refresh leaf");}
@@LEGION_BINDING@@
@@LEGION_CURRENT@@
@@SOURCE_ELIGIBLE@@
@@MSI_NEUTRAL@@
@@SOURCE_ADMISSION@@
@@READ_PROGRESS@@
@@ADAPTERS@@
static bool domainLogEnabled(const char*){return true;}
static json logs=json::array();static void appendNativeLifecycleLog(const char* domain,const json& j){logs.push_back({{"domain",domain},{"row",j}});}
@@NOTE@@
@@REQUEST_GUARD@@
static float inputHostNormalizeAxis(short x){return x/32767.0f;}
@@FRAME@@
static HANDLE inputHostCreateServerPipeLocked(){auto h=fake::allocate(false,++fake::nextEndpoint);DWORD e=0;if(!ymcc::inputHost::pipeTransport::processRuntime().adopt(h,L"peer-submission",e))return nullptr;return h;}
static bool inputHostFrameSubmissionMayIssue(void*) noexcept;
static void inputHostFrameSubmissionTransportRejected() noexcept;
@@PIPE_CLOSE@@
@@RECREATE_WRITE@@
@@PIPE_WRAPPERS@@
static bool fixtureSchedulingSelection=false;
static unsigned long long commandSequence=0;static unsigned faultBranches=0,newHosts=0,publicationNotes=0;static std::string lastRejectDetail;
static std::atomic<unsigned long long> g_inputHostHealthySinceTick{0};enum class InputHostPublicationStage{SourceAbsent};static void inputHostNotePublicationStage(InputHostPublicationStage){++publicationNotes;}
static DWORD g_inputHostSwitchRecoveryTransportError=ERROR_SUCCESS;
static auto& g_inputHostSequence=commandSequence;
static bool preparedRequest(const char* command,json* responseOut,const json& payload=json::object(),InputFrameSubmission* localFrameSubmission=nullptr,
                            InputHostRecreateWrite* recreateWrite=nullptr){
 (void)localFrameSubmission;
@@REQUEST_POWER_ADMISSION@@
@@PRE_ENVELOPE@@
@@FORGET_TARGET_INPUT@@
 std::string requestText="synthetic-envelope";DWORD timeoutMs=fixtureSchedulingSelection?1000:1;const std::uint64_t sequenceValue=commandSequence+1;
const struct {bool protectedCycle=false;std::uint64_t requestId=0;} rejectedReceiptOwner;
@@TRANSPORT@@
 (void)writeElapsedUs;(void)readElapsedUs;
 if(!read){++faultBranches;
@@FAILED_CONNECT_CLEANUP@@
 return false;}
 // Original DOM final-current block, with upstream parsing explicitly excluded.
 const bool advanceSequence=true;
@@LATE_ACK_TAIL@@
 ++commandSequence;return true;
}
@@LATE_ACK_CONSUMER@@
// Only the actual legacy request call is compiled here; frame materialization
// and remote tuple/fast parsing remain outside this original fragment fixture.
[[maybe_unused]] static bool inputHostRequestPreparedLocked(const char* command,const json& payload,const char*,DWORD,json* responseOut,
 const std::string*,const std::string*,const unsigned long long*,void*,bool,void*,bool,InputFrameSubmission* localFrameSubmission){
 return preparedRequest(command,responseOut,payload,localFrameSubmission);
}
[[maybe_unused]] static bool inputHostRequestLocked(const char* command,const json& payload,const char*,DWORD,json* responseOut){
 return preparedRequest(command,responseOut,payload);
}
static bool legacyFrameRequest(json* receiptOut,InputFrameSubmission& frameSubmission){
 (void)frameSubmission;const json fallbackFrame=json::object();
@@LEGACY_CALL@@
}

static bool g_inputHostActive=true;static std::string g_inputHostLifecycle,g_inputHostReleaseDisposition;
struct Result{bool sent=false,neutral=false,suppressed=false,contextRejected=false,transportFaultHandled=false;std::string hostSubmission;json frame;};
static Result submit(const InputCaptureGamepadSnapshot* original,bool physicalSourceAdmitted,bool semanticNeutral=false,const std::function<void()>& betweenLocks={},bool legacy=false,bool backendReject=false){
 const auto* t_commonFrameSource=original;
@@SETUP@@
@@POWER@@
 if(!powerAdmitted)return {};
@@NORMALIZE@@
 bool suppressPublication=false;
@@SUPPRESS@@
 Result r;r.neutral=semanticNeutral;r.suppressed=suppressPublication;if(suppressPublication)return r;
 FocusFrameContent frameContent;frameContent.buttons=0x1000;frameContent.backButtons=3;frameContent.lxShort=22222;frameContent.rawLtByte=250;frameContent.lt=1;frameContent.ltRaw=1;frameContent.gyroX=81;frameContent.imuAdmitted=true;frameContent.screenButtons=1024;
@@ASSEMBLY@@
 // The synthetic transport asserts canonical fields; materialize this test observation explicitly.
 (void)materializeFrame();
 r.frame=frame;if(betweenLocks)betweenLocks();
@@HOST_READY@@
 bool fault=false;bool accepted=false;json receipt;if(hostFrameReady){
 accepted=legacy?legacyFrameRequest(&receipt,frameSubmission):preparedRequest("SUBMIT_FRAME",&receipt,json::object(),@@LOCAL_FRAME_ARGUMENT@@);
 if(accepted&&backendReject){accepted=false;receipt={{"detail","device-backend-failed"}};}
 }
 if(accepted){}
 else if(@@LOCAL_CONTEXT_CONDITION@@){
@@NOFAULT@@
 }else if(hostFrameReady){++faultBranches;
@@TRANSPORT_UNUSABLE@@
 if(transportUnusable){
@@TRANSPORT_FAULT@@
 }else{const std::string rejectDetail=receipt.value("detail","");
@@BACKEND_HOLD@@
 }}
 r.sent=accepted;r.contextRejected=frameSubmission.rejected;r.transportFaultHandled=fault;
 json result;
@@SUBMISSION_RESULT@@
 r.hostSubmission=result.value("hostSubmission","");return r;
}
static InputCaptureGamepadSnapshot makeSample(const std::string& source="xinput",DWORD mask=1){InputCaptureGamepadSnapshot s;s.live=true;s.inputSource=source;s.connectedMask=mask;s.readAttemptSequence=9;s.readStartedTick=tick-2;s.readCompletedTick=tick-1;s.readPowerGeneration=power;s.readTopologyGeneration=g_xinputTopologyEpoch;s.readIdentityGeneration=static_cast<unsigned>(g_xinputSlotIdentityEpoch.load());s.readContextStable=true;readSharedSystemPower(&s.sharedPower,sizeof(s.sharedPower));s.readMsiSourceRevision=g_msiMainSourceRevision;s.readMsiModeGeneration=g_msiFeedbackModeGeneration;return s;}
static void reset(){
 fixtureOffer=FixtureOffer::Available;fixtureOfferCalls=fixtureForgottenTargets=0;
 g_inputHostFrameReplyBinding.clear();g_inputHostCompactFrameRequest=false;g_inputHostGameMotorBindingGeneration=0;
 g_inputHostRecoveryIntent.~VirtualRecoveryIntent();new(&g_inputHostRecoveryIntent)ymcc::inputHost::VirtualRecoveryIntent;
 g_inputHostRecoveryIntent.configure(json::object(),"pipe-fixture",true,true,"steamdeck");
 g_virtualStartProtection.~VirtualStartProtection();new(&g_virtualStartProtection)ymcc::VirtualStartProtection;
 g_inputHostHello=true;g_inputHostReleaseQuarantined=false;
 lastRejectDetail.clear();g_inputHostHealthySinceTick=0;g_inputHostSwitchRecoveryTransportError=ERROR_SUCCESS;g_inputHostInputPowerCache.reset();g_inputHostLifecyclePowerCache.reset();
 t_inputHostRequestPower.reset();t_inputHostRequestHardwareRequired=false;g_sharedPowerRevocations.revoke();
 fixturePublication=fixtureNotification=fixtureSuspendSerial=1;fixtureNotificationPending=false;fixtureKeepTarget=true;
 t_inputHostOneXStartWrite=false;t_inputHostDeferredPhysicalHideWrite=nullptr;t_inputHostDeferredPhysicalHideRefreshIssueActive=false;
 auto& rt=ymcc::inputHost::pipeTransport::processRuntime();if(g_inputHostServerPipe){fake::completeConnect=true;rt.close(g_inputHostServerPipe);rt.maintenance();g_inputHostServerPipe=nullptr;}fake::clear();g_inputHostProcess=reinterpret_cast<HANDLE>(std::uintptr_t(0x555));power=g_inputHostEpoch=1;g_inputHostSwitchRecoveryStartDeadline=0;g_inputHostPrepareMayHaveReachedHost=false;g_inputHostVirtualIntentRevision=0;g_inputHostVirtualIntentWriters=0;g_msiMainSourceRevision=7;g_msiFeedbackModeGeneration=1;t_legionMotionSubmission=nullptr;t_windowsMotionSubmission=nullptr;g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoTablet";g_hidResolveCacheGeneration=g_legionMotionCaptureEpoch=g_legionMotionEpochStartedAt=1;g_legionRawInputSubscribed=true;g_inputCaptureStop=false;g_powerLifecycle=PowerLifecycle::Ready;selectedMotionSensor=0;g_xinputTopologyEpoch=1;g_xinputSlotIdentityEpoch=1;g_inputReady=true;writerAllowed=true;exitFence=isMsi=false;g_inputHostPowerAdmissionRevoked=g_inputHostFaultHoldsStart=false;g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=g_inputHostPipeConnected=false;g_msiDinputRouteDinputArmed=false;g_msiDinputRouteConfirmed=g_msiDinputSourceValid=true;g_msiDinput.enabled=g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=0x1902;tick=1000;commandSequence=0;faultBranches=newHosts=publicationNotes=0;logs=json::array();g_inputHostReadProgress={};}
struct Checks{json cases=json::array(),failures=json::array();unsigned assertions=0;std::string name;bool collectOnly=false;std::vector<std::string> selected,planned;void check(bool b,const char* why){++assertions;if(!b)failures.push_back({{"case",name},{"assertion",why}});}template<class F>void run(const char* n,F f){if(collectOnly){planned.emplace_back(n);return;}if(!selected.empty()&&std::find(selected.begin(),selected.end(),n)==selected.end())return;fake::activeCase=name.c_str();reset();name=n;fake::activeCase=name.c_str();const auto before=failures.size();try{f();}catch(const std::exception&e){check(false,e.what());}cases.push_back({{"name",n},{"passed",failures.size()==before},{"writes",fake::writes},{"connects",fake::connects},{"sequence",commandSequence},{"faultBranches",faultBranches},{"newHosts",newHosts},{"logs",logs}});}};

// Parameterized additions to this original fixture/ID. All Win32 leaves remain
// deterministic; these are transport facts, not a synthetic remote command ACK.
struct BorrowedCancellationOwner {
 HANDLE event=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 ~BorrowedCancellationOwner(){if(event)CloseHandle(event);}
};
static void runPipeCancellationCases(Checks& c){
 using namespace ymcc::inputHost::pipeTransport;
 const auto open=[](){g_inputHostServerPipe=inputHostCreateServerPipeLocked();return g_inputHostServerPipe;};
 const auto observe=[](const ymcc::inputHost::pipeTransport::Result& r){const auto s=processRuntime().snapshot();logs.push_back({{"domain","pipe-transport"},{"row",{{"ok",r.ok},{"admitted",r.admitted},{"contextRejected",r.contextRejected},{"scopeRevoked",r.scopeRevoked},{"error",r.error},{"firstWait",r.firstWait},{"terminal",r.terminal},{"retired",r.retired},{"byteCountKnown",r.byteCountKnown},{"transferred",r.transferred},{"cancelAttempted",r.cancelAttempted},{"cancelError",r.cancelError},{"operation",r.operation},{"pending",s.pending},{"closeDebt",s.failedCloseDebt}}}});return r;};
 const auto close=[&c](HANDLE pipe){auto& rt=processRuntime();rt.close(pipe);g_inputHostServerPipe=nullptr;rt.maintenance();c.check(rt.stopped(),"all owned transport handles returned");};
 const auto validWaits=[&c](){c.check(!fake::waitMismatch&&fake::waitSteps.empty(),"exact scheduled wait kinds/budgets consumed once");c.check(fake::duplicateClosesWhilePending==0,"no IO handle closed while pending");for(const auto& w:fake::waits)logs.push_back({{"domain","pipe-wait"},{"row",{{"handles",w.count},{"budgetMs",w.milliseconds},{"begin",w.begin},{"end",w.end},{"result",w.result}}}});};
 const auto run=[&](const std::string& name,const std::function<void()>& body){c.run(("pipe-cancellation/"+name).c_str(),[&]{
  body();validWaits();
  auto& rt=processRuntime();
  const auto residual=rt.snapshot();
  if(!residual.stopped){
   // Record the unresolved original teardown before isolating the next case.
   c.check(false,"original case left runtime resources before fixture cleanup");
   logs.push_back({{"domain","fixture-failed-case-cleanup"},{"row",{{"pending",residual.pending},{"closeDebt",residual.failedCloseDebt}}}});
   fake::completePending(ERROR_OPERATION_ABORTED);
   if(g_inputHostServerPipe){rt.close(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;}
   rt.maintenance();
   fake::require(rt.stopped(),"failed case fixture cleanup did not return resources");
  }
 });};
 for(const auto kind:{Kind::Connect,Kind::Read,Kind::Write}){
  const std::string suffix=kind==Kind::Connect?"connect":kind==Kind::Read?"read":"write";
  run("pre-signaled-"+suffix,[&]{
   const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;std::weak_ptr<void> weak=owner;
   Cancellation cancel{borrowed,owner};owner.reset();SetEvent(borrowed);char data[2]{};
   const auto before=processRuntime().snapshot().admitted;
   const auto r=observe(processRuntime().run(pipe,kind,data,kind==Kind::Connect?0:2,100,7,nullptr,nullptr,std::move(cancel)));
   c.check(!r.admitted&&!r.ok&&r.contextRejected&&r.scopeRevoked&&r.error==ERROR_OPERATION_ABORTED,"pre-issue signal rejects without claiming remote cancellation");
   c.check(fake::writes+fake::reads+fake::connects==0&&fake::cancels==0&&processRuntime().snapshot().admitted==before,"no issue, cancellation or admission count on pre-signal");
   c.check(weak.expired()&&borrowed->closed,"unissued owner released exactly once");close(pipe);
  });
 }
 for(unsigned invalid=0;invalid<4;++invalid)run("invalid-pair-"+std::to_string(invalid),[&]{
  const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();Cancellation cancel{owner->event,owner};
  if(invalid==0)cancel.lifetime.reset();
  if(invalid==1)cancel.lifetime=std::shared_ptr<void>(std::shared_ptr<void>{},owner.get());
  if(invalid==2)cancel.event=nullptr;
  if(invalid==3)cancel.event=INVALID_HANDLE_VALUE;
  char data[2]{};const auto r=observe(processRuntime().run(pipe,Kind::Write,data,2,100,7,nullptr,nullptr,std::move(cancel)));
  c.check(!r.admitted&&!r.scopeRevoked&&!r.contextRejected&&r.error==ERROR_INVALID_PARAMETER,"incomplete/ownerless/invalid token rejected as parameter error");
  c.check(fake::writes+fake::reads+fake::connects==0&&fake::cancels==0,"bad token never issues or cancels IO");close(pipe);
 });
 run("owning-null-alias",[&]{
  const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;std::weak_ptr<void> weak=owner;
  Cancellation cancel{borrowed,std::shared_ptr<void>(owner,nullptr)};owner.reset();char data[2]{};
  const auto r=observe(processRuntime().run(pipe,Kind::Write,data,2,100,7,nullptr,nullptr,std::move(cancel)));
  c.check(r.ok&&!r.scopeRevoked&&weak.expired()&&borrowed->closed,"real control block retains owner even with null alias get()");
  c.check(processRuntime().snapshot().cachedOperations==1,"terminal cache retained without public owner");close(pipe);
 });
 for(unsigned preparation=0;preparation<2;++preparation)run("preparation-signal-"+std::to_string(preparation),[&]{
  const HANDLE pipe=open();char data[2]{};if(preparation)c.check(processRuntime().run(pipe,Kind::Write,data,2,100).ok,"warm original same-lease cache");
  const unsigned writes=fake::writes;auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;
  if(preparation)fake::resetHook=[=]{SetEvent(borrowed);};else fake::createHook=[=]{SetEvent(borrowed);};
  const auto r=observe(processRuntime().run(pipe,Kind::Write,data,2,100,7,nullptr,nullptr,{borrowed,owner}));
  c.check(!fake::createHook&&!fake::resetHook&&r.scopeRevoked&&r.contextRejected&&!r.admitted&&fake::writes==writes,"event preparation cannot bypass final pre-issue observation");close(pipe);
 });
 for(const auto kind:{Kind::Connect,Kind::Read,Kind::Write}){
  const std::string suffix=kind==Kind::Connect?"connect":kind==Kind::Read?"read":"write";
  for(unsigned order=0;order<3;++order)run(suffix+"-wait-order-"+std::to_string(order),[&]{
   const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;std::weak_ptr<void> weak=owner;
   Cancellation cancel{borrowed,owner};owner.reset();fake::pendingConnect=kind==Kind::Connect;fake::pendingReads=kind==Kind::Read;fake::pendingWrites=kind==Kind::Write;
   fake::queueWait(2,100,30,[=]{if(order!=0)SetEvent(borrowed);if(order!=1)fake::completePending();});
   if(order==1)fake::queueWait(1,7,2,[]{fake::completePending();});
   char data[2]{};const auto r=observe(processRuntime().run(pipe,kind,data,kind==Kind::Connect?0:2,100,7,nullptr,nullptr,std::move(cancel)));
   if(order==1){
    c.check(r.admitted&&!r.ok&&r.terminal&&!r.retired&&!r.contextRejected&&r.scopeRevoked&&r.error==ERROR_OPERATION_ABORTED,
     "ordinary issued IO stays cancelled even if terminal success arrives in the original cleanup grace");
    c.check(fake::cancels==1&&r.firstWait==WAIT_OBJECT_0+1,"scope-first ordinary IO cancels exactly once");
    c.check(fake::blockingBudgets()==std::vector<DWORD>{100,7}&&GetTickCount64()==1032,
     "ordinary cancellation never spends ACK drain balance or renews the original grace");
    if(kind!=Kind::Connect)c.check(!r.byteCountKnown&&r.transferred==0,"cancelled ordinary IO cannot expose late byte count");
    if(kind==Kind::Read)c.check(data[0]==0&&data[1]==0,"late bytes from an ordinary cancelled read are not an ACK");
   }else{
    c.check(r.admitted&&r.ok&&r.terminal&&!r.retired&&!r.contextRejected&&r.scopeRevoked==(order!=0),"IO and public-revocation facts remain independent");
    c.check(fake::cancels==0&&r.firstWait==WAIT_OBJECT_0,"simultaneous IO wins index zero; cancellation does not call CancelIoEx early");
    c.check(fake::blockingBudgets()==std::vector<DWORD>{100},"completed IO consumes only its original wait");
    if(kind!=Kind::Connect)c.check(r.byteCountKnown&&r.transferred==2,"successful IO still reports actual transferred bytes");
    if(kind==Kind::Read)c.check(data[0]=='{'&&data[1]=='}',"completed read preserves actual response bytes for wire accounting");
   }
   c.check(weak.expired()&&borrowed->closed&&processRuntime().snapshot().cachedOperations==1,"terminal same-lease cache does not pin public scope");close(pipe);
  });
 }
 run("inline-success-with-revocation",[&]{
  const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;
  fake::writeHook=[=]{SetEvent(borrowed);};char data[2]{};const auto r=observe(processRuntime().run(pipe,Kind::Write,data,2,100,7,nullptr,nullptr,{borrowed,owner}));
  c.check(r.ok&&r.scopeRevoked&&r.admitted&&!r.cancelAttempted&&r.byteCountKnown,"inline completed IO remains success with independent revoked scope fact");close(pipe);
 });
 run("preparation-time-consumes-budget",[&]{
  const HANDLE pipe=open();char data[2]{};c.check(processRuntime().run(pipe,Kind::Write,data,2,100).ok,"warm cache for actual ResetEvent boundary");
  auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;fake::pendingReads=true;fake::resetHook=[]{tick+=20;};
  fake::queueWait(2,80,30,[=]{SetEvent(borrowed);});fake::queueWait(1,50,20,[]{fake::completePending();});
  const auto r=observe(processRuntime().run(pipe,Kind::Read,data,2,100,7,nullptr,nullptr,{borrowed,owner,true}));
  c.check(r.ok&&r.scopeRevoked&&fake::blockingBudgets()==std::vector<DWORD>{80,50}&&GetTickCount64()==1070,"preparation plus cancellation never renew caller-supplied balance");
  c.check(fake::writes==1&&fake::reads==1&&fake::cancels==0,"bounded same-request drain reads one ACK without replay");close(pipe);
 });
 for(unsigned timing=0;timing<3;++timing)run("same-request-ack-drain-"+std::to_string(timing),[&]{
  const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;std::weak_ptr<void> weak=owner;char data[2]{};
  fake::writeHook=[=]{SetEvent(borrowed);};const auto write=observe(processRuntime().run(pipe,Kind::Write,data,2,100,7,nullptr,nullptr,{borrowed,owner}));
  c.check(write.ok&&write.scopeRevoked&&!write.contextRejected,"same request may already have reached Host before scope revoked");
  const auto denied=observe(processRuntime().run(pipe,Kind::Read,data,2,60,7,nullptr,nullptr,{borrowed,owner}));
  c.check(denied.contextRejected&&denied.scopeRevoked&&!denied.admitted&&fake::reads==0,"ordinary read after revocation cannot silently gain drain permission");
  Cancellation drain{borrowed,owner,true};owner.reset();
  if(timing!=0){fake::pendingReads=true;fake::queueWait(1,60,timing==1?20:60,[]{fake::completePending();});}
  const auto r=observe(processRuntime().run(pipe,Kind::Read,data,2,60,7,nullptr,nullptr,std::move(drain)));
  c.check(r.ok&&r.scopeRevoked&&!r.contextRejected&&r.admitted&&r.byteCountKnown&&r.transferred==2&&data[0]=='{'&&data[1]=='}',"explicit same-request drain returns actual ACK despite pre-signaled cancellation");
  c.check(fake::writes==1&&fake::reads==1&&fake::connects==0&&fake::cancels==0,"drain only reads one ACK and never reissues remote effect");
  c.check(weak.expired()&&borrowed->closed&&processRuntime().snapshot().cachedOperations==1,"ACK drain terminal cache releases shared registration");
  c.check(fake::blockingBudgets()==(timing==0?std::vector<DWORD>{}:std::vector<DWORD>{60}),"pre-signaled ACK drain consumes only caller-supplied original request balance");close(pipe);
 });
 for(const auto kind:{Kind::Connect,Kind::Write})run(kind==Kind::Write?"drain-cannot-write":"drain-cannot-connect",[&]{
  const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();SetEvent(owner->event);char data[2]{};
  const auto r=observe(processRuntime().run(pipe,kind,data,kind==Kind::Connect?0:2,60,7,nullptr,nullptr,{owner->event,owner,true}));
  c.check(!r.admitted&&!r.contextRejected&&!r.scopeRevoked&&r.error==ERROR_INVALID_PARAMETER&&fake::writes+fake::connects==0,"read-drain flag cannot authorize a new command or connection");close(pipe);
 });
 run("revoked-ack-drain-times-out",[&]{
  const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;std::weak_ptr<void> weak=owner;SetEvent(borrowed);
  Cancellation drain{borrowed,owner,true};owner.reset();fake::pendingReads=true;fake::cancelCompletes=true;fake::queueWait(1,60,60);char data[2]{};
  const auto r=observe(processRuntime().run(pipe,Kind::Read,data,2,60,7,nullptr,nullptr,std::move(drain)));
  c.check(!r.ok&&r.scopeRevoked&&!r.contextRejected&&r.admitted&&r.error==WAIT_TIMEOUT&&r.cancelAttempted&&r.terminal&&!r.retired,"ACK drain still times out and uses original CancelIoEx/grace path");
  c.check(weak.expired()&&borrowed->closed&&fake::blockingBudgets()==std::vector<DWORD>{60,7},"canceled-terminal drain returns scope without new timeout or replay");close(pipe);
 });
 for(unsigned outcome=0;outcome<3;++outcome)run("deadline-terminal-"+std::to_string(outcome),[&]{
  const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;std::weak_ptr<void> weak=owner;
  char data[2]{};
  c.check(processRuntime().run(pipe,Kind::Write,data,2,100).ok,"same request was issued before the cancellation-drain deadline");
  Cancellation drain{borrowed,owner,true};owner.reset();fake::pendingReads=true;
  const DWORD elapsed=outcome==2?100:30;fake::queueWait(2,100,elapsed,[=]{SetEvent(borrowed);});
  if(outcome!=2)fake::queueWait(1,70,70);
  fake::queueWait(1,7,outcome==0?2:7,[=]{if(outcome==0)fake::completePending();});
  const auto r=observe(processRuntime().run(pipe,Kind::Read,data,2,100,7,nullptr,nullptr,std::move(drain)));
  c.check(!r.ok&&r.error==WAIT_TIMEOUT&&r.scopeRevoked&&r.cancelAttempted&&fake::cancels==1,"original timeout remains timeout even if cleanup sees late success");
  c.check(fake::writes==1&&fake::reads==1&&!r.byteCountKnown&&data[0]==0&&data[1]==0,"late ACK cannot replay command or turn original timeout into business success");
  c.check(fake::blockingBudgets()==(outcome==2?std::vector<DWORD>{100,7}:std::vector<DWORD>{100,70,7}),"no fresh timeout after cancellation or expired deadline");
  c.check(GetTickCount64()<=1107,"same original IO budget plus original grace only");
  if(outcome==0)c.check(r.terminal&&!r.retired&&weak.expired()&&borrowed->closed,"late success in grace returns owner but does not rewrite original outcome");
  else{
   c.check(!r.terminal&&r.retired&&!weak.expired()&&!borrowed->closed&&processRuntime().snapshot().pending==1,"unreturned operation retains borrowed event owner after run returns");
   processRuntime().close(pipe);g_inputHostServerPipe=nullptr;c.check(processRuntime().snapshot().pending==1&&!weak.expired(),"close/disconnect cannot fake terminal resource return");
   const HANDLE ioEvent=fake::lastIssuedEvent;fake::closeFailureCounts[ioEvent]=1;fake::completePending();processRuntime().maintenance();
   const auto debt=processRuntime().snapshot();c.check(debt.pending==0&&debt.failedCloseDebt==1&&debt.retainedOperations==1,"terminal IO retains real event-close debt");
   c.check(weak.expired()&&borrowed->closed&&!ioEvent->closed,"terminal close debt no longer pins public cancellation scope");
  }
  close(pipe);
 });
 run("scope-wait-failure-is-not-revocation",[&]{
  const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;fake::failedProbe=borrowed;
  char data[2]{};const auto r=observe(processRuntime().run(pipe,Kind::Write,data,2,100,7,nullptr,nullptr,{borrowed,owner}));
  c.check(!r.admitted&&!r.scopeRevoked&&!r.contextRejected&&r.error==ERROR_ACCESS_DENIED&&fake::writes==0,"failed zero-time probe rejects issue without inventing cancellation");fake::failedProbe=nullptr;close(pipe);
 });
 run("multiwait-failure-retains-owner",[&]{
  const HANDLE pipe=open();auto owner=std::make_shared<BorrowedCancellationOwner>();const HANDLE borrowed=owner->event;std::weak_ptr<void> weak=owner;
  Cancellation cancel{borrowed,owner};owner.reset();fake::pendingWrites=true;fake::queueWait(2,100,3,{},ERROR_ACCESS_DENIED);fake::queueWait(1,7,7);
  char data[2]{};const auto r=observe(processRuntime().run(pipe,Kind::Write,data,2,100,7,nullptr,nullptr,std::move(cancel)));
  c.check(!r.ok&&!r.scopeRevoked&&r.error==ERROR_ACCESS_DENIED&&r.retired&&!weak.expired(),"wait infrastructure failure stays distinct and preserves pending owner");fake::completePending();close(pipe);c.check(weak.expired(),"later real terminal proof releases failed-wait owner");
 });
 run("legacy-default-pending",[&]{
  const HANDLE pipe=open();fake::pendingWrites=true;fake::queueWait(1,100,20,[]{fake::completePending();});char data[2]{};
  const auto r=observe(processRuntime().run(pipe,Kind::Write,data,2,100,7));
  c.check(r.ok&&!r.scopeRevoked&&fake::blockingWaitCount(2)==0&&fake::blockingBudgets()==std::vector<DWORD>{100},"omitted cancellation keeps original single-event wait behavior");close(pipe);
 });
}

#include "request_cases.inc"
static void runOriginalCases(Checks& c){
 if(!c.selected.empty())runLocalPublicVetoCases(c);
 c.run("normal-xinput",[&]{auto s=makeSample();auto r=submit(&s,true);c.check(r.sent&&!r.neutral&&fake::writes==1,"current normal frame writes");});
 c.run("unknown-route-becomes-full-neutral",[&]{auto s=makeSample("unknown");auto r=submit(&s,true);c.check(r.sent&&r.neutral&&r.frame["buttons"]==0&&r.frame["backButtons"]==0&&r.frame["raw"]["bLeftTrigger"]==0&&r.frame["imu"]["gyroDps"]["x"]==0,"unknown data cannot leak buttons/axes/IMU");});
 c.run("counterexample/source-absent-same-host-neutral",[&]{auto s=makeSample("none",0);s.live=false;auto r=submit(&s,false);c.check(r.neutral,"actual first-lock source code chooses semantic neutral");c.check(r.sent&&fake::writes==1,"same target must receive full neutral to release last held buttons even without source");});
 c.run("stale-read-to-neutral",[&]{auto s=makeSample();tick+=2000;auto r=submit(&s,true);c.check(r.sent&&r.neutral&&r.frame["buttons"]==0,"expired snapshot sends only neutral in original context");});
 c.run("msi-zero-connected-mask",[&]{isMsi=true;g_msiDinputRouteDinputArmed=true;auto s=makeSample("msi-dinput",0);auto r=submit(&s,true);c.check(r.sent&&!r.neutral,"confirmed MSI canonical source permits zero XInput mask");});
 c.run("msi-revision-retired",[&]{isMsi=true;g_msiDinputRouteDinputArmed=true;auto s=makeSample("msi-dinput",0);++g_msiMainSourceRevision;auto r=submit(&s,true);c.check(r.sent&&r.neutral,"stale MSI revision becomes neutral");});
 c.run("msi-xinput-fallback-disarmed",[&]{isMsi=true;auto s=makeSample();++g_msiMainSourceRevision;auto r=submit(&s,true);c.check(r.sent&&!r.neutral,"disarmed MSI XInput fallback does not require unrelated DInput revision");});
 c.run("xinput-zero-mask-rejected-as-source",[&]{auto s=makeSample("xinput",0);auto r=submit(&s,true);c.check(r.sent&&r.neutral,"zero XInput mask never authorizes source data");});
 c.run("semantic-neutral-absent-bypasses-source",[&]{auto s=makeSample("none",0);s.live=false;auto r=submit(&s,true,true);c.check(r.sent&&r.neutral,"existing caller live-or-neutral argument admits explicit neutral");});
 for(unsigned kind=0;kind<4;++kind)c.run(("neutral-connect-context-change-"+std::to_string(kind)).c_str(),[&]{auto s=makeSample("none",0);s.live=false;fake::connectHook=[&]{if(kind==0)++power;if(kind==1)++g_inputHostEpoch;if(kind==2)++g_inputHostVirtualIntentRevision;if(kind==3)++g_inputHostVirtualIntentWriters;};auto r=submit(&s,true,true);c.check(r.contextRejected&&fake::connects==1&&fake::writes==0&&commandSequence==0&&faultBranches==0&&newHosts==0,"neutral must not bypass original context after actual Connect");});
 c.run("normal-source-retired-during-connect",[&]{auto s=makeSample();fake::connectHook=[]{++g_xinputTopologyEpoch;};auto r=submit(&s,true);c.check(r.contextRejected&&fake::writes==0&&commandSequence==0&&faultBranches==0,"Connect source retirement does not write/consume command/fault");});
 c.run("counterexample/power-after-final-guard-before-WriteFile",[&]{auto s=makeSample();fake::resetHook=[]{++power;};auto r=submit(&s,true);c.check(!fake::resetHook&&fake::resets>0,"actual transport ResetEvent interleave executed after final request guard");c.check(!r.sent&&fake::writes==0&&commandSequence==0,"original power ticket must survive transport preparation before actual WriteFile");});
 c.run("counterexample/intent-after-final-guard-before-WriteFile",[&]{auto s=makeSample();fake::resetHook=[]{++g_inputHostVirtualIntentRevision;};auto r=submit(&s,true);c.check(!fake::resetHook&&fake::resets>0,"actual transport ResetEvent interleave executed");c.check(!r.sent&&fake::writes==0,"original intent must be rechecked at actual WriteFile");});
 c.run("connect-retired-context-rejected-resource-lifetime",[&]{auto s=makeSample();fake::pendingConnect=true;fake::connectHook=[]{++power;};auto r=submit(&s,true);auto& rt=ymcc::inputHost::pipeTransport::processRuntime();auto snapshot=rt.snapshot();c.check(r.contextRejected&&fake::writes==0&&snapshot.pending==1&&fake::cancels==1,"failed Connect cancellation retains live operation even on context early return");c.check(fake::duplicateClosesWhilePending==0,"context rejection never frees live issuing/event owner");rt.close(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;c.check(rt.snapshot().pending==1&&fake::duplicateClosesWhilePending==0,"closing original keeps issuing duplicate/event until completion");fake::completeConnect=true;rt.maintenance();c.check(rt.snapshot().pending==0,"terminal cancelled Connect reaps exact owned resources");});
 c.run("unbound-independent-lifecycle-neutral-legacy",[&]{auto s=makeSample();InputFrameSubmission parent;parent.ticket={1,1,0,false};InputFrameSubmissionScope parentScope(&parent);bool innerLegacy=false;{InputFrameSubmissionScope scope(nullptr);++power;innerLegacy=inputHostFrameSubmissionRequestCurrent("SUBMIT_FRAME",nullptr,"legacy");}c.check(innerLegacy&&t_inputFrameSubmission==&parent,"independent unbound neutral preserves old contract and restores outer scope");(void)s;});
 // Current dependencies must execute actual guards, not merely compile via placeholders.
 c.run("msi-feedback-generation-retired-at-WriteFile",[&]{isMsi=true;g_msiDinputRouteDinputArmed=true;auto s=makeSample("msi-dinput",0);fake::resetHook=[]{++g_msiFeedbackModeGeneration;};auto r=submit(&s,true);c.check(!fake::resetHook&&fake::resets>0,"actual terminal issue interleave ran");c.check(r.contextRejected&&!r.sent&&fake::writes==0&&commandSequence==0&&faultBranches==0,"MSI mode generation retirement cannot issue, consume sequence or fault");});
 c.run("windows-motion-current",[&]{auto binding=std::make_shared<std::atomic<std::uint64_t>>(1);ymcc::windows_motion::Stamp stamp;stamp.activeBinding=binding;stamp.bindingGeneration=1;t_windowsMotionSubmission=&stamp;auto s=makeSample();auto r=submit(&s,true);c.check(r.sent&&fake::writes==1,"current actual Windows motion stamp permits existing transport");});
 c.run("windows-motion-retired-at-WriteFile",[&]{auto binding=std::make_shared<std::atomic<std::uint64_t>>(1);ymcc::windows_motion::Stamp stamp;stamp.activeBinding=binding;stamp.bindingGeneration=1;t_windowsMotionSubmission=&stamp;auto s=makeSample();fake::resetHook=[binding]{binding->store(2);};auto r=submit(&s,true);c.check(!fake::resetHook&&fake::resets>0,"actual Windows motion terminal interleave ran");c.check(r.contextRejected&&!r.sent&&fake::writes==0&&commandSequence==0&&faultBranches==0,"actual Windows motion guard rejects source retirement before WriteFile");});
 c.run("windows-motion-publication-pending",[&]{auto binding=std::make_shared<std::atomic<std::uint64_t>>(1);ymcc::windows_motion::Stamp stamp;stamp.activeBinding=binding;stamp.bindingGeneration=1;stamp.captureLease=std::make_shared<std::atomic<bool>>(true);t_windowsMotionSubmission=&stamp;auto s=makeSample();auto r=submit(&s,true);c.check(r.contextRejected&&!r.sent&&fake::writes==0&&commandSequence==0&&faultBranches==0,"canonical Windows capture without publication proof cannot issue");bool pending=false;for(const auto& row:logs)pending=pending||row["row"].value("reason",std::string{})=="windows-source-publication-not-committed";c.check(pending,"actual adapter retains publication-pending distinction");});
 c.run("controller-motion-current",[&]{ymcc::legion_motion::Sample motion;motion.context={power,1};motion.source.captureEpoch=1;motion.source.sensorIndex=0;motion.source.capabilityToken=legionMotionModelBinding().token();motion.capturedAt=tick-1;t_legionMotionSubmission=&motion;auto s=makeSample();auto r=submit(&s,true);c.check(r.sent&&fake::writes==1,"verbatim current controller-motion guard permits existing transport");});
 c.run("controller-motion-retired-at-WriteFile",[&]{ymcc::legion_motion::Sample motion;motion.context={power,1};motion.source.captureEpoch=1;motion.source.sensorIndex=0;motion.source.capabilityToken=legionMotionModelBinding().token();motion.capturedAt=tick-1;t_legionMotionSubmission=&motion;auto s=makeSample();fake::resetHook=[]{++g_legionMotionCaptureEpoch;};auto r=submit(&s,true);c.check(!fake::resetHook&&fake::resets>0,"actual controller-motion terminal interleave ran");c.check(r.contextRejected&&!r.sent&&fake::writes==0&&commandSequence==0&&faultBranches==0,"verbatim controller-motion guard rejects retired capture before WriteFile");});
 c.run("controller-motion-model-change-at-WriteFile",[&]{ymcc::legion_motion::Sample motion;motion.context={power,1};motion.source.captureEpoch=1;motion.source.sensorIndex=0;motion.source.capabilityToken=legionMotionModelBinding().token();motion.capturedAt=tick-1;t_legionMotionSubmission=&motion;auto s=makeSample();fake::resetHook=[]{g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoSZ1";};auto r=submit(&s,true);c.check(!fake::resetHook&&fake::resets>0,"model changes at the actual terminal issue boundary");c.check(r.contextRejected&&!r.sent&&fake::writes==0&&commandSequence==0&&faultBranches==0,"old model capability token cannot issue through the new total binding");});
 c.run("controller-motion-missing-capability-token",[&]{ymcc::legion_motion::Sample motion;motion.context={power,1};motion.source.captureEpoch=1;motion.source.sensorIndex=0;motion.capturedAt=tick-1;t_legionMotionSubmission=&motion;auto s=makeSample();auto r=submit(&s,true);c.check(r.sent&&r.neutral&&r.frame["buttons"]==0&&r.frame["backButtons"]==0&&r.frame["imu"]["gyroDps"]["x"]==0,"missing total capability token must release the same target with complete neutral");});
 c.run("controller-motion-unknown-model",[&]{g_machineIdentity.deviceClass="HandheldCompanion.Devices.Unknown";ymcc::legion_motion::Sample motion;motion.context={power,1};motion.source.captureEpoch=1;motion.source.sensorIndex=0;motion.source.capabilityToken=1;motion.capturedAt=tick-1;t_legionMotionSubmission=&motion;auto s=makeSample();auto r=submit(&s,true);c.check(r.sent&&r.neutral&&r.frame["buttons"]==0&&r.frame["raw"]["bLeftTrigger"]==0&&r.frame["imu"]["admitted"]==false,"unknown model cannot supply motion or buttons but must not block neutral release");});
}

// Explicit manual selection only: default all retains the previous case set.
static void runSchedulingDelayCases(Checks& c){
 const auto close=[](){auto& rt=ymcc::inputHost::pipeTransport::processRuntime();
  if(g_inputHostServerPipe){rt.close(g_inputHostServerPipe);g_inputHostServerPipe=nullptr;}rt.maintenance();
  return rt.stopped();};
 const auto pending=[](){fake::pendingReads=true;fake::cancelCompletes=true;};
 for(const auto delay:{5000u,10000u,60000u}){
  for(const bool neutral:{false,true}){
   const std::string name="scheduling-delay/"+std::to_string(delay)+(neutral?"-neutral":"-input");
   c.run(name.c_str(),[&]{FakeHandle host{true,false,false,0};g_inputHostProcess=&host;pending();
    fake::queueWait(2,1000,1000);
    for(unsigned ms=1100;ms<=delay;ms+=100)fake::queueWait(1,100,100,ms==delay?std::function<void()>([]{fake::completePending();}):std::function<void()>{});
    const auto sample=makeSample();const auto begin=tick;const auto result=submit(&sample,true,neutral);
    c.check(result.sent&&tick-begin==delay,"one delayed ACK remains accepted on the existing target");
    c.check(fake::writes==1&&fake::reads==1&&fake::cancels==0&&faultBranches==0&&newHosts==0,"no replay, cancellation, teardown or replacement for scheduling delay");
    c.check(fake::waitSteps.empty()&&!fake::waitMismatch,"exact planned waits consumed once");c.check(close(),"original owner returned");
   });
  }
 }
 for(unsigned edge=0;edge<4;++edge){
  const std::string name="scheduling-delay/revoked-"+std::to_string(edge);
  c.run(name.c_str(),[&]{FakeHandle host{true,false,false,0};g_inputHostProcess=&host;pending();
   fake::queueWait(2,1000,1000);fake::queueWait(1,100,100,[&]{
    if(edge==0)g_inputHostRecoveryIntent.invalidate();
    if(edge==1)exitFence=true;
    if(edge==2){fixtureNotificationPending=true;g_sharedPowerRevocations.revoke();}
    if(edge==3)++g_inputHostEpoch;
   });
   const auto sample=makeSample();const auto begin=tick;const auto result=submit(&sample,true);
   c.check(!result.sent&&tick-begin<=1200,"OFF/exit/power/foreign Host revoke prolonged wait promptly");
   c.check(fake::writes==1&&fake::reads==1&&newHosts==0,"revocation cannot replay command or create target");c.check(close(),"revoked issued IO still returns actual owner");
  });
 }
 c.run("scheduling-delay/finite-ceiling",[&]{FakeHandle host{true,false,false,0};g_inputHostProcess=&host;pending();
  const auto sample=makeSample();const auto begin=tick;const auto result=submit(&sample,true);
  c.check(!result.sent&&tick-begin>=65000&&tick-begin<=65050,"real unresponsive transport remains bounded by65seconds plus existing cleanup");
  c.check(fake::writes==1&&fake::reads==1&&fake::cancels==1,"expiry retains exactly one issued read and one cancellation");c.check(close(),"expired operation closes owned transport");
 });
 c.run("scheduling-delay/minute-idle-does-not-disconnect",[&]{FakeHandle host{true,false,false,0};g_inputHostProcess=&host;
  tick+=60000;c.check(fake::writes==0&&faultBranches==0&&newHosts==0,"no input for60seconds has no synthetic pipe failure");
  const auto sample=makeSample();const auto result=submit(&sample,true,true);
  c.check(result.sent&&faultBranches==0&&newHosts==0,"first fresh neutral frame after idle retains existing Host");c.check(close(),"idle transport closes only explicitly");
 });
}

int main(int argc,char** argv){Checks c;std::string caseGroup="all";bool groupSet=false;
 for(int i=1;i<argc;++i){const std::string arg=argv[i];
  if(arg=="--case-group"&&!groupSet&&i+1<argc){caseGroup=argv[++i];groupSet=true;}
  else if(arg=="--case"&&i+1<argc){const std::string name=argv[++i];if(name.empty()||name.starts_with("--")||std::find(c.selected.begin(),c.selected.end(),name)!=c.selected.end()){std::cerr<<"empty/duplicate original case\n";return 2;}c.selected.push_back(name);}
  else{std::cerr<<"unknown/missing/duplicate option\n";return 2;}
 }
 if(caseGroup!="all"&&caseGroup!="original"&&caseGroup!="pipe-cancellation"&&caseGroup!="request-shared-power"&&caseGroup!="scheduling-delay"){std::cerr<<"unknown/empty case group\n";return 2;}
 fixtureSchedulingSelection=caseGroup=="scheduling-delay";
 if(!c.selected.empty()){
  if(!groupSet)caseGroup="original";
  if(caseGroup!="original"){std::cerr<<"--case requires original group\n";return 2;}
  // Enumerate the original definitions without invoking/resetting any case.
  // Unknown selectors fail before any original test body or IO runs.
  c.collectOnly=true;runOriginalCases(c);c.collectOnly=false;
  for(const auto& name:c.selected)if(std::find(c.planned.begin(),c.planned.end(),name)==c.planned.end()){std::cerr<<"unknown original case: "<<name<<'\n';return 2;}
 }
 if(caseGroup=="all"||caseGroup=="original")runOriginalCases(c);
 if(caseGroup=="all"||caseGroup=="pipe-cancellation")runPipeCancellationCases(c);
 if(caseGroup=="scheduling-delay")runSchedulingDelayCases(c);
 if(caseGroup=="all"||caseGroup=="request-shared-power")runRequestSharedPowerCases(c);
 reset();c.check(!c.cases.empty(),"selected original fixture group is nonempty");c.check(targetCreateEntries==0,"SUBMIT_FRAME never enters target-creation boundary");auto result=json{{"caseGroup",caseGroup},{"targetCreateEntries",targetCreateEntries},{"passed",c.failures.empty()},{"tests",c.cases.size()},{"assertions",c.assertions},{"failures",c.failures},{"cases",c.cases}};std::cout<<result.dump(2)<<'\n';return c.failures.empty()?0:1;}


