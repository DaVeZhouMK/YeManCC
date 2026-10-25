// Exact production policy/current/sample/issue bodies are inserted by the runner.
// Windows types only; no OS API, pipe, Host, driver or device is executed.
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
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
static std::atomic<ULONGLONG> power{1},g_inputHostVirtualIntentRevision{1},g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};
static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1},g_inputHostVirtualIntentWriters{0};
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false},g_msiDinputRouteDinputArmed{false};
static bool g_inputHostProcess=true,g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostTransportFault=false;
static ULONGLONG g_inputHostEpoch=7,now=100;
static bool exitFence=false,msiEligible=true;
static ULONGLONG currentPowerGeneration(){return power;}
static ULONGLONG GetTickCount64Inert(){return now;}
#define GetTickCount64 GetTickCount64Inert
static bool inputHostExitFenceRaised(){return exitFence;}
static bool msiClawKnownBoardAuthority(){return g_machineIdentity.family==YmccFamily::MsiClaw;}
static bool msiDinputMainSourceEligible(){return msiEligible;}
static bool msiControllerNeutralCleanupAllowed(bool value){return value&&msiClawKnownBoardAuthority();}
@@KEEP@@
@@WRITER@@
@@SNAPSHOT@@
@@FRAME@@
@@SOURCE_ADMITTED@@
@@SAMPLE@@
@@CURRENT@@
// Motion is outside this sidecar. All frame motion pointers remain null; reject any accidental Legion use.
static bool legionMotionSubmissionCurrent(const ymcc::legion_motion::Sample&,ULONGLONG) noexcept {return false;}
@@WINDOWS_CURRENT@@
@@MAY_ISSUE@@
static std::vector<std::string> cases,failures;
static unsigned assertions=0;
static void require(bool value,const char* message){++assertions;if(!value)failures.push_back(cases.back()+": "+message);}
static InputCaptureGamepadSnapshot sample(){InputCaptureGamepadSnapshot s;s.live=true;s.connectedMask=1;s.inputSource="xinput";s.readAttemptSequence=1;s.readPowerGeneration=power;s.readMsiSourceRevision=1;s.readMsiModeGeneration=1;s.readStartedTick=s.readCompletedTick=now;s.readTopologyGeneration=g_xinputTopologyEpoch;s.readIdentityGeneration=g_xinputSlotIdentityEpoch;s.readContextStable=true;return s;}
static InputFrameSubmission frame(const InputCaptureGamepadSnapshot& s,bool neutral=false){InputFrameSubmission f{};f.ticket={g_inputHostEpoch,power.load(),g_inputHostVirtualIntentRevision.load(),neutral};f.sample=inputHostFrameSubmissionSample(&s);f.physicalSourceAdmitted=true;return f;}
static void reset(){g_machineIdentity.family=YmccFamily::Unknown;aoac=false;g_powerLifecycle=PowerLifecycle::Ready;power=1;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_msiDinputRouteDinputArmed=false;g_inputHostProcess=g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=false;g_inputHostEpoch=7;g_inputHostVirtualIntentRevision=1;g_inputHostVirtualIntentWriters=0;g_msiMainSourceRevision=1;g_xinputTopologyEpoch=g_xinputSlotIdentityEpoch=1;now=100;exitFence=false;msiEligible=true;}
template<class F>static void run(const std::string& name,F test){reset();cases.push_back(name);test();}
int main(){
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
 run("old-ticket-cannot-cross-sleep-generation",[]{auto f=frame(sample());++power;require(!inputHostFrameSubmissionMayIssue(&f),"old request cannot issue after new generation");require(f.reason==V::PowerChanged,"reason binds request generation");});
 run("retained-host-needs-new-generation-read",[]{const auto old=sample();++power;auto f=frame(old);require(!inputHostFrameSubmissionMayIssue(&f),"new ticket cannot make old sample current");require(f.reason==V::SourceRetired,"source window generation is checked separately");auto fresh=frame(sample());require(inputHostFrameSubmissionMayIssue(&fresh),"new read resumes without recreating target");require(g_inputHostEpoch==7,"Host target identity remains unchanged");});
 run("read-crossing-power-edge-is-not-relabelled",[]{auto s=sample();++power;s.readPowerGeneration=power;s.readContextStable=false;auto f=frame(s);require(!inputHostFrameSubmissionMayIssue(&f),"unstable actual read context remains retired even if its generation scalar matches");require(f.reason==V::SourceRetired,"crossed read is source retirement");});
 run("static-zero-input-remains-valid",[]{auto s=sample();for(unsigned i=0;i<5;++i){auto f=frame(s);require(inputHostFrameSubmissionMayIssue(&f),"unchanged current zero sample does not require button/packet change");++now;}require(s.pad.wButtons==0,"case remains neutral physical input");});
 run("neutral-source-exception-still-bound-to-power",[]{auto s=sample();s.live=false;s.connectedMask=0;s.inputSource="none";auto f=frame(s,true);require(inputHostFrameSubmissionMayIssue(&f),"same-target neutral cleanup does not require a live source");++power;require(!inputHostFrameSubmissionMayIssue(&f),"neutral cleanup cannot cross power generation");require(f.reason==V::PowerChanged,"neutral does not waive request identity");});
 run("msi-source-eligibility-remains-separate",[]{g_machineIdentity.family=YmccFamily::MsiClaw;g_msiDinputRouteDinputArmed=true;auto s=sample();s.inputSource="msi-dinput";s.connectedMask=0;msiEligible=false;auto f=frame(s);require(!inputHostFrameSubmissionMayIssue(&f),"ready is insufficient when MSI reader is not eligible");require(f.reason==V::SourceUnavailable,"source state is distinct from power state");msiEligible=true;f=frame(s);require(inputHostFrameSubmissionMayIssue(&f),"eligible current MSI sample resumes without target restart");});
 std::cout<<json({{"cases",cases},{"assertions",assertions},{"failures",failures},{"noOsOrDeviceCalls",true},{"actualPowerCallbacksExecuted",false}}).dump(2)<<std::endl;
 return failures.empty()?0:1;
}
