// Actual production fragments. OS read/conversion and power events are inert boundaries.
#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <mutex>
#include <functional>
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
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound{true};
using json=nlohmann::json;
static ULONGLONG clockTick=1000;static ULONGLONG fixtureTick(){return clockTick;}
#define GetTickCount64 fixtureTick
static std::atomic<ULONGLONG> power{1},g_msiFeedbackModeGeneration{1},g_msiMainSourceRevision{1},g_inputHostVirtualIntentRevision{1};
static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1},g_inputHostVirtualIntentWriters{0};
static ULONGLONG currentPowerGeneration(){return power;}
struct Capability{unsigned pidDInput=0x1902;};static Capability kMsiClawCapability;
static bool knownMsi=true;static bool msiClawKnownBoardAuthority(){return knownMsi;}
static std::atomic<bool> g_msiDinputRouteDinputArmed{true},g_msiDinputRouteConfirmed{true},g_msiDinputSourceValid{true};
static std::atomic<ULONGLONG> g_msiFeedbackSamplePower{0},g_msiFeedbackSampleMode{0};
static std::atomic<unsigned> g_msiFeedbackSampleTopology{0};
@@PROOF_TYPE@@
struct MsiDinputState{
 std::atomic<bool> enabled{true},mainInterfaceMatched{true};std::atomic<unsigned> productId{0x1902};unsigned bindCount=1;
@@CANONICAL_FIELDS@@
};
static MsiDinputState g_msiDinput;
#ifdef YMCC_MSI_REPORT_CHAIN_DIAGNOSTIC
static std::atomic<uint64_t> g_msiCanonicalReportSequence{0};
#endif
static void appendNativeLifecycleLog(const char*,const json&){}
@@PUBLISH@@
@@INVALIDATE@@
@@SNAPSHOT@@
static void msiDinputClearOwnedState(const char*){g_msiDinputSourceValid=false;g_msiDinputRouteConfirmed=false;msiDinputInvalidateCanonicalFrame();}
@@PERMIT_TYPE@@
@@GATE_TYPE@@
static MsiSampleGate g_msiSampleGate;
@@ACQUIRE@@
@@RELEASE@@
@@COMMIT@@
@@ELIGIBLE@@
@@CAPTURE_TYPE@@
// Feedback routing is outside this source-publication proof; no output backend runs.
static void inputCaptureStampFeedbackSourceLocked(InputCaptureGamepadSnapshot& sample){sample.feedbackSource={};}
@@APPLY@@
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
static DWORD g_xinputPrimarySlot=0;
// Successful, range-validated canonical payload is supplied at an inert OS boundary.
// Start/commit statements are extracted from the actual pump, never reimplemented here.
static void successfulPumpRead(const XINPUT_GAMEPAD& canonical,ULONGLONG duration=0,const std::function<void()>& duringRead={}){
 const ULONGLONG now=GetTickCount64();(void)now;
@@PRODUCER_BEGIN@@
 clockTick+=duration;if(duringRead)duringRead();
@@PRODUCER_COMPLETION@@
@@PRODUCER_GENERATION@@
@@PRODUCER_PUBLISH@@
@@PRODUCER_FEEDBACK@@
}
static void readState(){
@@READ_BEGIN@@
@@READ_LOCALS@@
 unsigned admittedMask=0;bool externalPriority=false;
@@FALLBACK@@
@@READ_SEQUENCE@@
@@READ_ASSIGNMENT@@
}
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false};
static ULONGLONG g_inputHostEpoch=7;static bool g_inputHostProcess=true,g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostTransportFault=false;
static bool writerAllowed=true;static bool sgVirtualWriterAllowedNow(){return writerAllowed;}static bool inputHostExitFenceRaised(){return false;}static bool msiControllerNeutralCleanupAllowed(bool v){return v&&knownMsi;}
@@FRAME_TYPE@@
@@SOURCE_ADMITTED@@
@@SAMPLE@@
@@CURRENT@@
// Motion is outside this sidecar. All frame motion pointers remain null; reject any accidental Legion use.
static bool legionMotionSubmissionCurrent(const ymcc::legion_motion::Sample&,ULONGLONG) noexcept {return false;}
@@WINDOWS_CURRENT@@
@@MAY_ISSUE@@
static std::vector<std::string> cases,failures;static unsigned assertions=0;static json observations=json::array();
static void require(bool b,const char* message){++assertions;if(!b)failures.push_back(cases.back()+": "+message);}
static bool mayIssue(){InputFrameSubmission f{};f.ticket={g_inputHostEpoch,power.load(),g_inputHostVirtualIntentRevision.load(),false};f.sample=inputHostFrameSubmissionSample(&g_inputCaptureGamepadSnapshot);f.physicalSourceAdmitted=true;const bool allowed=inputHostFrameSubmissionMayIssue(&f);observations.push_back({{"case",cases.back()},{"allowed",allowed},{"reason",ymcc::input::frameSubmission::name(f.reason)},{"buttons",g_inputCaptureGamepadSnapshot.pad.wButtons},{"samplePower",g_inputCaptureGamepadSnapshot.readPowerGeneration},{"currentPower",power.load()},{"readStart",g_inputCaptureGamepadSnapshot.readStartedTick},{"readEnd",g_inputCaptureGamepadSnapshot.readCompletedTick},{"now",clockTick}});return allowed;}
static void reset(){power=1;g_xinputTopologyEpoch=g_xinputSlotIdentityEpoch=1;g_msiFeedbackModeGeneration=g_msiMainSourceRevision=1;clockTick=1000;g_msiDinput.enabled=g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=0x1902;g_msiDinputRouteDinputArmed=g_msiDinputRouteConfirmed=g_msiDinputSourceValid=true;g_inputReady=true;g_inputHostPowerAdmissionRevoked=g_inputHostFaultHoldsStart=false;g_inputHostProcess=g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=false;writerAllowed=true;msiDinputInvalidateCanonicalFrame();g_inputCaptureGamepadSnapshot={};g_msiSampleGate.gen=1;g_msiSampleGate.closing=false;g_msiSampleGate.activeReads=g_msiSampleGate.quarantinedReads=0;}
template<class F>static void run(const char* name,F fn){reset();cases.push_back(name);fn();}
static XINPUT_GAMEPAD pressed(){XINPUT_GAMEPAD p{};p.wButtons=XINPUT_GAMEPAD_A;return p;}
int main(){
 run("stationary-current-source-still-valid",[]{successfulPumpRead({});readState();require(g_inputCaptureGamepadSnapshot.live,"zero source remains live");require(mayIssue(),"zero source is admissible");});
 run("bus-power-boundary-does-not-relabel-old-canonical",[]{successfulPumpRead(pressed());clockTick=23000;++power;readState();successfulPumpRead({});require(g_inputCaptureGamepadSnapshot.pad.wButtons==XINPUT_GAMEPAD_A,"BUS initially selected previous canonical payload");require(!mayIssue(),"old physical read cannot become current just because wrapper and later pump ran in new power");++clockTick;readState();require(mayIssue(),"next copied new-generation physical read resumes without recreate");require(g_inputCaptureGamepadSnapshot.pad.wButtons==0,"old button does not replay");});
 run("canonical-age-is-not-reset-by-wrapper",[]{successfulPumpRead(pressed());clockTick=2501;readState();require(!mayIssue(),"1501ms old producer cannot get new freshness from copy");});
 run("canonical-1500ms-inclusive-control",[]{successfulPumpRead({});clockTick=2500;readState();require(mayIssue(),"existing inclusive producer age bound is preserved");});
 run("bus-topology-boundary-does-not-relabel-old-canonical",[]{successfulPumpRead(pressed());++g_xinputTopologyEpoch;readState();successfulPumpRead({});require(!mayIssue(),"old topology payload cannot inherit wrapper topology");readState();require(mayIssue(),"current source next copy resumes");});
 run("source-revision-boundary-does-not-relabel-old-canonical",[]{successfulPumpRead(pressed());++g_msiMainSourceRevision;readState();require(!mayIssue(),"cached payload cannot inherit replacement source revision");successfulPumpRead({});readState();require(mayIssue(),"new source publication resumes");});
 run("mode-boundary-does-not-relabel-old-canonical",[]{successfulPumpRead(pressed());++g_msiFeedbackModeGeneration;readState();require(!mayIssue(),"old mode payload cannot inherit wrapper context");successfulPumpRead({});readState();require(mayIssue(),"new mode read resumes");});
 run("mode-change-after-copy-is-rechecked-at-issue",[]{successfulPumpRead(pressed());readState();++g_msiFeedbackModeGeneration;require(!mayIssue(),"mode retirement after snapshot is still blocked at final issue");successfulPumpRead({});readState();require(mayIssue(),"fresh current mode resumes without target rearm");});
 run("long-device-read-not-hidden-by-later-copy",[]{successfulPumpRead(pressed(),1501);readState();require(!mayIssue(),"copy after a long device read does not erase its start time");successfulPumpRead({});readState();require(mayIssue(),"subsequent quick read resumes");});
 run("unchanged-payload-new-read-refreshes-validity",[]{successfulPumpRead({});clockTick=2501;successfulPumpRead({});readState();require(mayIssue(),"same all-zero payload with actual new read is current");});
 run("retired-canonical-validity-still-blocks",[]{successfulPumpRead(pressed());msiDinputInvalidateCanonicalFrame();readState();require(!g_inputCaptureGamepadSnapshot.live,"invalidated source is not selected");require(!mayIssue(),"invalidated payload cannot publish");});
 run("closing-permit-does-not-allow-cached-read",[]{successfulPumpRead(pressed());g_msiSampleGate.closing=true;readState();require(!g_inputCaptureGamepadSnapshot.live,"closing source does not obtain a read permit");require(!mayIssue(),"closed source stays unavailable");});
 run("power-changes-during-real-read-still-revoked",[]{successfulPumpRead(pressed(),1,[]{++power;});readState();require(!g_inputCaptureGamepadSnapshot.live,"actual producer generation guard clears source");require(!mayIssue(),"unstable OS read cannot publish");});
 run("target-ready-does-not-override-input-not-ready",[]{successfulPumpRead(pressed());readState();g_inputReady=false;require(!mayIssue(),"sleep admission blocks even a current source");});
 std::cout<<json({{"cases",cases},{"assertions",assertions},{"failures",failures},{"observations",observations},{"noDeviceIo",true},{"wholePumpExecuted",false},{"wholeBusExecuted",false},{"hostExecuted",false}}).dump(2)<<std::endl;return failures.empty()?0:1;
}
