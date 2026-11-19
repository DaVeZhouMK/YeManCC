// Exact production policy/current/sample/issue bodies are inserted by the runner.
// Actual Windows cancellation events execute; no pipe, Host, driver or physical device executes.
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <algorithm>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <iostream>
#include <string>
#include <vector>
#include "json.hpp"
#include "input_frame_submission.h"
#include "touch_haptic_dispatch.h"
#include "game_feedback_receipt.h"
#include "legion_motion_source.h"
#include "windows_motion_submission.h"
#include "msi_virtual_report_diagnostic.h"
#include "HcControllerInputCapabilityCatalog.h"
#include "input_host_shared_power.h"
// Explicit policy facts; no installed application or output device is used.
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound{true};
using json=nlohmann::json;
@@POWER_ENUM@@
@@FAMILY_ENUM@@
struct MachineIdentity{YmccFamily family=YmccFamily::Unknown;};
static MachineIdentity g_machineIdentity;
static bool aoac=false;
static bool sgPlatformIsAlwaysOnAlwaysConnected(){return aoac;}
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<ULONGLONG> fixturePowerGeneration{1},g_inputHostVirtualIntentRevision{1},g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};
static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1},g_inputHostVirtualIntentWriters{0};
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false},g_msiDinputRouteDinputArmed{false};
static bool g_inputHostProcess=true,g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostTransportFault=false;
static ULONGLONG g_inputHostEpoch=7,now=100;
static bool exitFence=false,msiEligible=true;
static ULONGLONG currentPowerGeneration(){return fixturePowerGeneration;}
static ULONGLONG GetTickCount64Inert(){return now;}
#define GetTickCount64 GetTickCount64Inert
static bool inputHostExitFenceRaised(){return exitFence;}
static bool msiClawKnownBoardAuthority(){return g_machineIdentity.family==YmccFamily::MsiClaw;}
static bool msiDinputMainSourceEligible(){return msiEligible;}
static bool msiControllerNeutralCleanupAllowed(bool value){return value&&msiClawKnownBoardAuthority();}
// Controlled Reader port projects only the original fixture power inputs.
// The production public ABI, observation, mirror and Scope implementations are real.
namespace sp=ymcc::sleeppower;
namespace ip=ymcc::inputHost::sharedPower;
static std::atomic<ULONGLONG> publication{1},notification{1},suspendSerial{1};
static bool pendingNotification=false,hardwareAllowed=true,readerAvailable=true;
static sp::RevocationMirrors g_sharedPowerRevocations;
static ip::Cache g_inputHostInputPowerCache;
static unsigned readerCalls=0,excludedLifecycleCalls=0;
static int __cdecl readSharedSystemPower(sp::SharedPowerV1* output,std::uint32_t bytes){
    ++readerCalls;
    if(!readerAvailable||!output||bytes!=sizeof(*output))return 0;
    sp::SharedPowerV1 value;
    value.generation=currentPowerGeneration();value.publicationRevision=publication.load();
    value.observedNotificationSequence=notification.load();value.suspendSerial=suspendSerial.load();
    value.phase=static_cast<std::uint32_t>(g_powerLifecycle.load());
    value.flags=sp::kKnown|(hardwareAllowed&&value.phase==0?sp::kHardwareAllowed:0u)|
        (pendingNotification?sp::kNotificationPending:0u);
    *output=value;return 1;
}
@@TLS_SCOPE@@
static thread_local bool t_inputHostRequestHardwareRequired=false;
@@REQUEST_SCOPE@@
// Excluded lifecycle leaves are fail-closed, and every case asserts no entry.
static thread_local bool t_inputHostOneXStartWrite=false;
static bool inputHostOneXStartMayIssue(){++excludedLifecycleCalls;return false;}
struct InputHostDeferredPhysicalHide;
static thread_local const InputHostDeferredPhysicalHide* t_inputHostDeferredPhysicalHideWrite=nullptr;
static thread_local bool t_inputHostDeferredPhysicalHideRefreshIssueActive=false;
static bool inputHostDeferredPhysicalHideReserveRefreshIssue(){++excludedLifecycleCalls;return false;}
static bool inputHostDeferredPhysicalHideWriteCurrent(){++excludedLifecycleCalls;return false;}
@@KEEP@@
@@WRITER@@
@@SNAPSHOT@@
@@FRAME@@
@@BIND_STANDARD_INPUT@@
@@SOURCE_ADMITTED@@
@@SAMPLE@@
@@CURRENT@@
// Motion is outside this sidecar. All frame motion pointers remain null; reject any accidental Legion use.
static bool legionMotionSubmissionCurrent(const ymcc::legion_motion::Sample&,ULONGLONG) noexcept {return false;}
@@WINDOWS_CURRENT@@
@@SHARED_CURRENT@@
@@MAY_ISSUE@@
static std::vector<std::string> cases,failures;
static unsigned assertions=0;
static void require(bool value,const char* message){++assertions;if(!value)failures.push_back(cases.back()+": "+message);}
static InputCaptureGamepadSnapshot sample(){InputCaptureGamepadSnapshot s;s.live=true;s.connectedMask=1;s.inputSource="xinput";s.readAttemptSequence=1;s.readPowerGeneration=fixturePowerGeneration;s.readMsiSourceRevision=1;s.readMsiModeGeneration=1;s.readStartedTick=s.readCompletedTick=now;s.readTopologyGeneration=g_xinputTopologyEpoch;s.readIdentityGeneration=g_xinputSlotIdentityEpoch;s.readContextStable=true;readSharedSystemPower(&s.sharedPower,sizeof(s.sharedPower));return s;}
static InputFrameSubmission frame(const InputCaptureGamepadSnapshot& s,bool neutral=false){InputFrameSubmission f{};f.ticket={g_inputHostEpoch,fixturePowerGeneration.load(),g_inputHostVirtualIntentRevision.load(),neutral};f.sample=inputHostFrameSubmissionSample(&s);f.physicalSourceAdmitted=true;f.sourcePower=s.sharedPower;f.powerScope=g_inputHostInputPowerCache.acquire(&readSharedSystemPower,g_sharedPowerRevocations,sgShouldKeepInputDuringStandby());return f;}
static void reset(){g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=true;g_inputHostInputPowerCache.reset();t_inputHostRequestPower.reset();g_sharedPowerRevocations.revoke();publication=notification=suspendSerial=1;pendingNotification=false;hardwareAllowed=readerAvailable=true;readerCalls=excludedLifecycleCalls=0;t_inputHostOneXStartWrite=false;t_inputHostDeferredPhysicalHideWrite=nullptr;t_inputHostDeferredPhysicalHideRefreshIssueActive=false;g_machineIdentity.family=YmccFamily::Unknown;aoac=false;g_powerLifecycle=PowerLifecycle::Ready;fixturePowerGeneration=1;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_msiDinputRouteDinputArmed=false;g_inputHostProcess=g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=false;g_inputHostEpoch=7;g_inputHostVirtualIntentRevision=1;g_inputHostVirtualIntentWriters=0;g_msiMainSourceRevision=1;g_xinputTopologyEpoch=g_xinputSlotIdentityEpoch=1;now=100;exitFence=false;msiEligible=true;}
static bool collecting=true;
static std::string selectedCase;
static std::vector<std::string> registeredCases;
template<class F>static void run(const std::string& name,F test){
    if(collecting){registeredCases.push_back(name);return;}
    if(!selectedCase.empty()&&selectedCase!=name)return;
    reset();cases.push_back(name);
    try{test();}catch(const std::exception& error){require(false,error.what());}
    catch(...){require(false,"unexpected fixture exception");}
    require(excludedLifecycleCalls==0,"no excluded OneX/deferred lifecycle leaf executed");
    g_inputHostInputPowerCache.reset();t_inputHostRequestPower.reset();
}
static void suite(bool sharedOnly){
 using V=ymcc::input::frameSubmission::Verdict;
 struct Profile{const char* name;YmccFamily family;bool aoac,keep;};
 for(const auto& profile:std::vector<Profile>{{"MSI",YmccFamily::MsiClaw,false,true},{"ROG",YmccFamily::AsusRogAllyX,false,true},{"GPD",YmccFamily::Gpd,false,true},{"AYANEO",YmccFamily::Ayanero,false,true},{"OneX",YmccFamily::OneXPlayer,false,true},{"SteamMachine-PC",YmccFamily::ValveSteamMachine,false,false},{"SteamMachine-AoAc",YmccFamily::ValveSteamMachine,true,true},{"unknown-PC",YmccFamily::Unknown,false,false},{"unknown-AoAc",YmccFamily::Unknown,true,true}}){
  for(const auto& phase:std::vector<std::pair<const char*,PowerLifecycle>>{{"ready",PowerLifecycle::Ready},{"suspending",PowerLifecycle::Suspending},{"suspended",PowerLifecycle::Suspended},{"resuming",PowerLifecycle::Resuming}}){
   for(bool neutral:{false,true})run(std::string(profile.name)+"/"+phase.first+(neutral?"/neutral":"/live"),[&]{g_machineIdentity.family=profile.family;aoac=profile.aoac;g_powerLifecycle=phase.second;auto f=frame(sample(),neutral);const bool expected=phase.second==PowerLifecycle::Ready||(phase.second==PowerLifecycle::Resuming&&profile.keep);require(sgShouldKeepInputDuringStandby()==profile.keep,"policy distinguishes handheld/AoAc from ordinary PC");require(inputHostFrameSubmissionMayIssue(&f)==expected,"resident Host does not bypass power phase");require(f.rejected==!expected,"rejection is recorded");if(!expected)require(f.reason==V::PowerUnavailable,"sleep stop is power unavailability, not transport failure");});
  }
 }
 for(bool neutral:{false,true}){
  run(neutral?"sleep-edge-after-preparation-neutral":"sleep-edge-after-preparation-live",[&]{auto f=frame(sample(),neutral);require(inputHostFrameSubmissionMayIssue(&f),"ready preparation initially admissible");g_powerLifecycle=PowerLifecycle::Suspending;g_inputReady=false;require(!inputHostFrameSubmissionMayIssue(&f),"issue boundary sees later suspend");require(f.reason==V::PowerUnavailable,"suspend does not become backend fault");require(g_inputHostProcess&&g_inputHostPrepared&&g_inputHostNeutralized,"target retention is compatible with blocked writes");});
  run(neutral?"resuming-input-not-ready-neutral":"resuming-input-not-ready-live",[&]{g_machineIdentity.family=YmccFamily::MsiClaw;g_powerLifecycle=PowerLifecycle::Resuming;g_inputReady=false;auto f=frame(sample(),neutral);require(!inputHostFrameSubmissionMayIssue(&f),"keep policy cannot override explicit input-not-ready");});
  run(neutral?"resuming-revoked-neutral":"resuming-revoked-live",[&]{g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_powerLifecycle=PowerLifecycle::Resuming;g_inputHostPowerAdmissionRevoked=true;auto f=frame(sample(),neutral);require(!inputHostFrameSubmissionMayIssue(&f),"keep policy cannot override explicit admission revocation");});
 }
 run("old-ticket-cannot-cross-sleep-generation",[]{auto f=frame(sample());++fixturePowerGeneration;require(!inputHostFrameSubmissionMayIssue(&f),"old request cannot issue after new generation");require(f.reason==V::PowerChanged,"reason binds request generation");});
 run("retained-host-needs-new-generation-read",[]{const auto old=sample();++fixturePowerGeneration;auto f=frame(old);require(!inputHostFrameSubmissionMayIssue(&f),"new ticket cannot make old sample current");require(f.reason==V::SourceRetired,"source window generation is checked separately");auto fresh=frame(sample());require(inputHostFrameSubmissionMayIssue(&fresh),"new read resumes without recreating target");require(g_inputHostEpoch==7,"Host target identity remains unchanged");});
 run("read-crossing-power-edge-is-not-relabelled",[]{auto s=sample();++fixturePowerGeneration;s.readPowerGeneration=fixturePowerGeneration;s.readContextStable=false;auto f=frame(s);require(!inputHostFrameSubmissionMayIssue(&f),"unstable actual read context remains retired even if its generation scalar matches");require(f.reason==V::SourceRetired,"crossed read is source retirement");});
 run("static-zero-input-remains-valid",[]{auto s=sample();for(unsigned i=0;i<5;++i){auto f=frame(s);require(inputHostFrameSubmissionMayIssue(&f),"unchanged current zero sample does not require button/packet change");++now;}require(s.pad.wButtons==0,"case remains neutral physical input");});
 run("neutral-source-exception-still-bound-to-power",[]{auto s=sample();s.live=false;s.connectedMask=0;s.inputSource="none";auto f=frame(s,true);require(inputHostFrameSubmissionMayIssue(&f),"same-target neutral cleanup does not require a live source");++fixturePowerGeneration;require(!inputHostFrameSubmissionMayIssue(&f),"neutral cleanup cannot cross power generation");require(f.reason==V::PowerChanged,"neutral does not waive request identity");});
 run("msi-source-eligibility-remains-separate",[]{g_machineIdentity.family=YmccFamily::MsiClaw;g_msiDinputRouteDinputArmed=true;auto s=sample();s.inputSource="msi-dinput";s.connectedMask=0;msiEligible=false;auto f=frame(s);require(!inputHostFrameSubmissionMayIssue(&f),"ready is insufficient when MSI reader is not eligible");require(f.reason==V::SourceUnavailable,"source state is distinct from power state");msiEligible=true;f=frame(s);require(inputHostFrameSubmissionMayIssue(&f),"eligible current MSI sample resumes without target restart");});
 if(sharedOnly){
 // These extend the original ID; all original family/phase/neutral assertions above remain.
 for(bool neutral:{false,true}){
  run(neutral?"shared/notification-final-neutral":"shared/notification-final-live",[&]{
   auto f=frame(sample(),neutral);auto scope=f.powerScope;
   require(scope&&inputHostFrameSubmissionMayIssue(&f),"old exact final issue initially admitted");
   ++notification;g_sharedPowerRevocations.revoke();
   require(fixturePowerGeneration==1,"notification did not invent a generation");
   require(scope&&WaitForSingleObject(scope->event(),0)==WAIT_OBJECT_0,"real public cancellation event signaled");
   require(!inputHostFrameSubmissionMayIssue(&f)&&f.rejected&&f.reason==V::PowerUnavailable,"same-generation notification revokes final issue without backend fault");
   auto fresh=frame(sample(),neutral);require(inputHostFrameSubmissionMayIssue(&fresh),"new current source and scope resume same target");
   require(g_inputHostEpoch==7,"no replacement Host or persona");
  });
  run(neutral?"shared/suspend-serial-neutral":"shared/suspend-serial-live",[&]{
   auto f=frame(sample(),neutral);++suspendSerial;g_sharedPowerRevocations.revoke();
   require(!inputHostFrameSubmissionMayIssue(&f)&&f.rejected,"suspendSerial revokes despite same generation and Ready");
   require(f.reason==V::PowerUnavailable,"serial cancellation is not transport failure");
   auto fresh=frame(sample(),neutral);require(inputHostFrameSubmissionMayIssue(&fresh),"current replacement observation admitted");
  });
  run(neutral?"shared/pending-ready-neutral":"shared/pending-ready-live",[&]{
   auto f=frame(sample(),neutral);auto old=f.powerScope;pendingNotification=true;g_sharedPowerRevocations.revoke();
   require(!sgVirtualWriterAllowedNow(),"pending rejects public writer before UI phase change");
   require(!inputHostFrameSubmissionMayIssue(&f),"pending rejects final issue");
   pendingNotification=false;++notification;
   require(!inputHostFrameSubmissionMayIssue(&f),"Ready cannot revive cancelled old scope");
   auto fresh=frame(sample(),neutral);
   require(fresh.powerScope&&fresh.powerScope!=old&&inputHostFrameSubmissionMayIssue(&fresh),"new Ready scope and read recover");
   require(old&&WaitForSingleObject(old->event(),0)==WAIT_OBJECT_0,"Ready never resets borrowed old event");
  });
 }
 run("shared/fresh-read-older-cached-scope-publication",[]{
  auto old=frame(sample());auto scope=old.powerScope;const HANDLE event=scope->event();
  ++publication;auto fresh=frame(sample());
  require(fresh.sourcePower.publicationRevision>scope->power().publicationRevision,"source really follows cached publication");
  require(fresh.powerScope==scope&&fresh.powerScope->event()==event,"healthy cache and original event reused");
  require(inputHostFrameSubmissionMayIssue(&fresh),"fresh read and older healthy scope share same public window");
 });
 run("shared/older-read-newer-scope-publication",[]{
  auto source=sample();++publication;auto fresh=frame(source);
  require(fresh.sourcePower.publicationRevision<fresh.powerScope->power().publicationRevision,"source really precedes new scope publication");
  require(inputHostFrameSubmissionMayIssue(&fresh),"older current read and new scope share same public window");
 });
 run("shared/source-publication-ahead-of-reader-rejected",[]{
  auto source=sample();++source.sharedPower.publicationRevision;auto fresh=frame(source);
  require(fresh.powerScope&&fresh.powerScope->current(),"scope itself remains genuinely current");
  require(!inputHostFrameSubmissionMayIssue(&fresh)&&fresh.rejected&&fresh.reason==V::SourceRetired,"unobserved future source publication is never accepted");
 });
 run("shared/publication-only-cache-reuse",[]{
  auto s=sample();auto f=frame(s);auto first=f.powerScope;
  require(first&&inputHostFrameSubmissionMayIssue(&f),"initial exact issue");
  const HANDLE borrowed=first?first->event():nullptr;
  for(unsigned i=0;i<1001;++i){
   ++publication;auto next=frame(s);
   require(next.powerScope==first,"diagnostic publication reuses exact cached Scope");
   require(next.powerScope&&next.powerScope->event()==borrowed,"publication does not replace event");
   require(inputHostFrameSubmissionMayIssue(&next),"old valid public read survives publication-only advance");
  }
  require(borrowed&&WaitForSingleObject(borrowed,0)==WAIT_TIMEOUT,"healthy cached event stays clear");
 });
 for(unsigned edge=0;edge<2;++edge)run(edge?"shared/old-read-new-serial-scope":"shared/old-read-new-notification-scope",[&]{
  auto old=sample();if(edge)++suspendSerial;else ++notification;g_sharedPowerRevocations.revoke();
  auto f=frame(old);require(f.powerScope&&f.powerScope->current(),"new scope is genuinely current");
  require(!inputHostFrameSubmissionMayIssue(&f)&&f.rejected&&f.reason==V::SourceRetired,"new scope never relabels old same-generation physical read");
  auto fresh=frame(sample());require(inputHostFrameSubmissionMayIssue(&fresh),"actual fresh public read permits same Host");
 });
 run("shared/neutral-stale-source-release",[]{
  auto old=sample();++notification;g_sharedPowerRevocations.revoke();
  auto live=frame(old);require(!inputHostFrameSubmissionMayIssue(&live)&&live.reason==V::SourceRetired,"same-generation stale source rejected");
  auto neutral=frame(old,true);neutral.physicalSourceAdmitted=false;
  require(inputHostFrameSubmissionMayIssue(&neutral),"same-target current-scope neutral releases old state without physical source");
  ++g_inputHostVirtualIntentRevision;
  require(!inputHostFrameSubmissionMayIssue(&neutral)&&neutral.reason==V::IntentChanged,"neutral release still respects newer intent");
 });
 run("shared/cache-reset-borrowed-lifetime",[]{
  std::weak_ptr<ip::Scope> weak;HANDLE borrowed=nullptr;
  {
   auto f=frame(sample());weak=f.powerScope;borrowed=f.powerScope?f.powerScope->event():nullptr;
   auto again=frame(sample());require(f.powerScope&&again.powerScope==f.powerScope,"frames share current original cache");
   again.powerScope.reset();g_inputHostInputPowerCache.reset();
   require(!weak.expired(),"in-flight frame retains registration after cache reset");
   require(borrowed&&WaitForSingleObject(borrowed,0)==WAIT_OBJECT_0,"retained event is still valid and cancelled");
   require(!inputHostFrameSubmissionMayIssue(&f),"retained borrower cannot issue after reset");
  }
  require(weak.expired(),"last exact frame owner returns registration");
 });
 run("shared/phase-aba-old-scope-stays-cancelled",[]{
  auto f=frame(sample());g_powerLifecycle=PowerLifecycle::Suspending;g_sharedPowerRevocations.revoke();
  g_powerLifecycle=PowerLifecycle::Ready;
  require(!inputHostFrameSubmissionMayIssue(&f),"public mirror prevents scalar Ready ABA revival");
  auto fresh=frame(sample());require(inputHostFrameSubmissionMayIssue(&fresh),"new observation can recover after same scalar Ready");
 });
 run("shared/off-overrides-late-current-scope",[]{
  auto f=frame(sample());++g_inputHostVirtualIntentRevision;g_realStickTest.virtualTargetEnabled=false;
  g_inputHostInputPowerCache.reset();++publication;
  require(!inputHostFrameSubmissionMayIssue(&f)&&f.rejected&&f.reason==V::PowerUnavailable,"OFF cannot be overridden by late publication or Ready");
  auto late=frame(sample());require(!inputHostFrameSubmissionMayIssue(&late),"fresh public scope does not reopen OFF intent");
  require(!g_realStickTest.virtualTargetEnabled&&g_inputHostEpoch==7,"final guard never changes intent or recreates Host");
 });
 run("shared/new-intent-overrides-late-old-frame",[]{
  auto f=frame(sample());++g_inputHostVirtualIntentRevision;
  require(!inputHostFrameSubmissionMayIssue(&f)&&f.reason==V::IntentChanged,"original verdict precedes extra scope checks");
  auto fresh=frame(sample());require(inputHostFrameSubmissionMayIssue(&fresh),"fresh matching intent still works");
 });
 run("shared/request-tls-cancel-is-context-rejection",[]{
  auto f=frame(sample());InputHostRequestPowerScope request(f.powerScope);
  require(inputHostFrameSubmissionMayIssue(&f),"request uses original current frame scope");
  g_sharedPowerRevocations.revoke();
  require(!inputHostFrameSubmissionMayIssue(&f)&&f.rejected&&f.reason==V::PowerUnavailable,"cancelled TLS marks frame rejected rather than fault-no-write");
  require(!inputHostFrameSubmissionMayIssue(nullptr),"cancelled request cannot bypass with null frame");
 });
 run("shared/old-request-cannot-borrow-new-frame",[]{
  auto old=frame(sample());
  {
   InputHostRequestPowerScope request(old.powerScope);++notification;g_sharedPowerRevocations.revoke();
   auto fresh=frame(sample());require(fresh.powerScope&&fresh.powerScope->current(),"new frame scope current");
   require(!inputHostFrameSubmissionMayIssue(&fresh)&&fresh.rejected,"old request retains its cancellation across fresh frame");
  }
  require(!t_inputHostRequestPower,"exact production RAII restores prior empty TLS");
  auto fresh=frame(sample());require(inputHostFrameSubmissionMayIssue(&fresh),"new request/frame recovers after old request return");
 });
 run("shared/nested-request-restores-original",[]{
  auto f=frame(sample());auto original=f.powerScope;
  {
   InputHostRequestPowerScope outer(original);
   require(inputHostFrameSubmissionMayIssue(nullptr),"current explicit request works without frame");
   {InputHostRequestPowerScope inner({});require(!inputHostFrameSubmissionMayIssue(nullptr),"unbound context gains no permission");}
   require(t_inputHostRequestPower==original,"nested RAII restores exact prior scope");
  }
  require(!t_inputHostRequestPower,"outer request returned TLS");
 });
 run("shared/missing-public-scope-fails-closed",[]{
  auto f=frame(sample());f.powerScope.reset();
  require(!inputHostFrameSubmissionMayIssue(&f)&&f.rejected&&f.reason==V::PowerUnavailable,"legacy sample alone is not final permission");
 });
 run("shared/reader-unknown-and-ready-gate",[]{
  auto f=frame(sample());readerAvailable=false;
  require(!sgVirtualWriterAllowedNow()&&!inputHostFrameSubmissionMayIssue(&f),"UNKNOWN reader fails closed");
  readerAvailable=true;hardwareAllowed=false;
  auto closed=frame(sample());require(!inputHostFrameSubmissionMayIssue(&closed),"Ready without public hardware gate is not admission");
  hardwareAllowed=true;auto fresh=frame(sample());require(inputHostFrameSubmissionMayIssue(&fresh),"restored actual public gate permits new scope");
 });
 }
}


int main(int argc,char** argv){
 bool sharedOnly=false;
 for(int i=1;i<argc;++i){
  const std::string arg=argv[i];
  if(arg=="--shared-power-only"&&!sharedOnly)sharedOnly=true;
  else if(arg=="--case"&&i+1<argc&&selectedCase.empty()){
   selectedCase=argv[++i];if(selectedCase.empty()||selectedCase.rfind("--",0)==0)return 2;
  }else return 2;
 }
 if(!sharedOnly&&!selectedCase.empty())return 2;
 suite(sharedOnly); // Registration only: no reset, Reader, event, test body or device call.
 auto unique=registeredCases;std::sort(unique.begin(),unique.end());
 if(std::adjacent_find(unique.begin(),unique.end())!=unique.end())return 2;
 if(!selectedCase.empty()&&std::find(registeredCases.begin(),registeredCases.end(),selectedCase)==registeredCases.end())return 2;
 const auto selectedCases=selectedCase.empty()?registeredCases:std::vector<std::string>{selectedCase};
 if(selectedCases.empty())return 2;
 DWORD handlesBefore=0,handlesAfter=0;
 if(!GetProcessHandleCount(GetCurrentProcess(),&handlesBefore))return 3;
 collecting=false;suite(sharedOnly);
 if(cases!=selectedCases){cases.push_back("selection");require(false,"actual cases must exactly equal original selected registration order");}
 g_inputHostInputPowerCache.reset();t_inputHostRequestPower.reset();
 if(!GetProcessHandleCount(GetCurrentProcess(),&handlesAfter)||handlesAfter!=handlesBefore){
  cases.push_back("cleanup");require(false,"all exact owned real Windows event handles returned");
 }
 std::cout<<json({{"cases",cases},{"selectedCases",selectedCases},{"selection",sharedOnly?"shared-power-only":"default"},
  {"selectedCase",selectedCase},{"assertions",assertions},{"failures",failures},
  {"handlesBefore",handlesBefore},{"handlesAfter",handlesAfter},{"actualWindowsEvents",true},
  {"readerPort","controlled original fixture inputs"},{"actualPowerCallbacksExecuted",false},
  {"realDeviceIo",false},{"wholePipeOrHostChain",false},{"fullParentExecuted",!sharedOnly},{"noOsOrDeviceCalls",false},{"noDeviceIo",true}}).dump(2)<<std::endl;
 return failures.empty()?0:1;
}
