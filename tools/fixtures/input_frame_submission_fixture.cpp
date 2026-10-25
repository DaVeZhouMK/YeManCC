// Inert OS/transport boundaries. Named production blocks are inserted verbatim.
#include "json.hpp"
#include "input_frame_submission.h"
#include "touch_haptic_dispatch.h"
#include "game_feedback_receipt.h"
#include "windows_motion_submission.h"
#include "legion_motion_source.h"
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
// Explicit policy facts; no installed application or output device is used.
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound{true};
using json=nlohmann::json;using ULONGLONG=unsigned long long;using DWORD=unsigned;
struct XINPUT_GAMEPAD{unsigned short wButtons=0;};
static bool g_xinputSpecialButton=true;static DWORD g_xinputPrimarySlot=0,g_xinputConnectedMask=1;
static ULONGLONG now=100;static ULONGLONG GetTickCount64(){return now;}
static std::atomic<ULONGLONG> power{1},g_inputHostVirtualIntentRevision{0},g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0},g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1};
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false},g_msiDinputRouteDinputArmed{false};
static ULONGLONG g_inputHostEpoch=7;static ULONGLONG inputHostEpochToken(){return g_inputHostEpoch;}static ULONGLONG currentPowerGeneration(){return power;}
static bool g_inputHostProcess=true,g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostTransportFault=false,g_inputHostActive=true;
static bool exitFence=false,writerAllowed=true,knownMsi=false,msiEligible=true;
static bool inputHostExitFenceRaised(){return exitFence;}static bool sgVirtualWriterAllowedNow(){return writerAllowed;}static bool msiClawKnownBoardAuthority(){return knownMsi;}static bool msiDinputMainSourceEligible(){return msiEligible;}static bool msiControllerNeutralCleanupAllowed(bool neutral){return neutral&&knownMsi;}
static bool logThrows=false;static std::vector<json> logs;static void appendNativeLifecycleLog(const char*,const json& row){if(logThrows)throw std::runtime_error("inert evidence failure");logs.push_back(row);}
struct InputCaptureGamepadSnapshot {
    bool live=true;XINPUT_GAMEPAD pad{};DWORD primarySlot=0;unsigned connectedMask=1;std::string inputSource="xinput";
    ULONGLONG readAttemptSequence=1,readStartedTick=100,readCompletedTick=100,readPowerGeneration=1,readMsiSourceRevision=1,readMsiModeGeneration=1;
    unsigned readTopologyGeneration=1,readIdentityGeneration=1;bool readContextStable=true;ymcc::touchhaptic::SourceToken feedbackSource{};bool specialXbox=false;
};
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
static void captureReadSnapshot(const std::string& inputSource="xinput",DWORD admittedMask=1){
 const bool live=true;const XINPUT_GAMEPAD pad{};
@@SNAPSHOT_GUIDE@@
}
static json sourceDiagnostic(){return json{
@@UI_SOURCE_FIELD@@
};}
static const InputCaptureGamepadSnapshot* t_commonFrameSource=nullptr;
static thread_local const ymcc::legion_motion::Sample* t_legionMotionSubmission=nullptr;
static thread_local const ymcc::windows_motion::Stamp* t_windowsMotionSubmission=nullptr;
static bool legionMotionSubmissionCurrent(const ymcc::legion_motion::Sample&,ULONGLONG) noexcept{return true;}
// Serial feedback remains an inert terminal outside frame/power acceptance.
struct InputFrameSubmission;
static void nativeSubmitGameFeedbackFrame(const InputFrameSubmission&,bool);
static void gamepadSerialRevokeGameFeedback() {}
@@CONTEXT@@
static void nativeSubmitGameFeedbackFrame(const InputFrameSubmission&,bool) {}
@@ADAPTERS@@
namespace ymcc::screenpads {struct Snapshot{bool present=false;};static json snapshotJson(const Snapshot& s){return {{"present",s.present}};}}
static float inputHostNormalizeAxis(short value){return static_cast<float>(value)/32768.0f;}
@@FRAME@@
enum class InputHostPublicationStage{SourceAbsent};static void inputHostNotePublicationStage(InputHostPublicationStage){}
static std::atomic<ULONGLONG> g_inputHostHealthySinceTick{0};
struct InputHostFastReceipt{};
static unsigned pipeWrites=0,commandSequence=0,failuresFromHost=0;static bool connectFails=false;
static bool freshNeutralReady=true;static unsigned freshNeutralCalls=0;
static bool inputHostFreshNeutralReady(bool,const XINPUT_GAMEPAD&,unsigned){++freshNeutralCalls;return freshNeutralReady;}
static unsigned g_xinputAdmittedMask=1;
static std::function<void()> connectHook,afterAssemblyHook;
static json wire=json::object();static json pendingFrame;
static bool request(const char* command,json* responseOut){
@@PRE_ENVELOPE@@
 if(connectHook){auto hook=std::move(connectHook);connectHook={};hook();}
@@PRE_PIPE@@
 if(connectFails){++failuresFromHost;return false;}
 ++pipeWrites;++commandSequence;wire=pendingFrame;return true;
}
static bool inputHostRequestPreparedLocked(const char* command,const json& frame,const char*,int,json* responseOut,const std::string*,const std::string*,const unsigned long long*,InputHostFastReceipt*){pendingFrame=frame;return request(command,responseOut);}
static bool inputHostRequestLocked(const char* command,const json& frame,const char*,int,json* responseOut){pendingFrame=frame;return request(command,responseOut);}
struct Result{bool accepted=false,neutral=false,rejected=false,fault=false;json receipt;};
static Result submit(const InputCaptureGamepadSnapshot* sample,bool semanticNeutral=false,bool fast=true,const std::function<void()>& afterTicket={}){
 t_commonFrameSource=sample;bool physicalSourceAdmitted=true;
@@SETUP@@
@@POWER@@
 if(!powerAdmitted){t_commonFrameSource=nullptr;return {};}
 if(afterTicket)afterTicket();
@@NORMALIZE@@
 if(!inputHostPhysicalPublicationSourceAdmitted(physicalSourceAdmitted,semanticNeutral)){t_commonFrameSource=nullptr;return {};}
 const bool msiNeutralCleanup=msiControllerNeutralCleanupAllowed(semanticNeutral);const XINPUT_GAMEPAD pad{0x1001};
@@GUIDE_ASSEMBLY@@
@@FRESH_WAIT@@
  t_commonFrameSource=nullptr;return {};
 }
 FocusFrameContent frameContent;frameContent.buttons=outputPad.wButtons;frameContent.backButtons=3;frameContent.lxShort=1234;frameContent.lyShort=-4321;frameContent.rxShort=5678;frameContent.ryShort=-2345;frameContent.lt=0.5f;frameContent.rt=1.0f;frameContent.ltRaw=0.5f;frameContent.rtRaw=1.0f;frameContent.rawLtByte=127;frameContent.rawRtByte=255;frameContent.imuAdmitted=true;frameContent.gyroX=12;frameContent.gyroY=3;frameContent.accelZ=1;frameContent.qx=1;frameContent.screenButtons=1;frameContent.touchpads.present=true;
@@ASSEMBLY@@
 // The synthetic transport asserts canonical fields; materialize this test observation explicitly.
 (void)materializeFrame();
 if(afterAssemblyHook){auto hook=std::move(afterAssemblyHook);afterAssemblyHook={};hook();}
@@HOST_READY@@
 bool accepted=false,fault=false;std::string lastRejectDetail;json receipt;
 if(hostFrameReady){
  const bool frameTextReady=fast;std::string frameRequestText="inert",frameRequestId="inert";unsigned long long frameSequence=commandSequence+1;InputHostFastReceipt fastReceipt;
  const auto submitFrameLegacy=[&](json* responseOut){return inputHostRequestLocked("SUBMIT_FRAME",frame,"frame-accepted",1000,responseOut);};
@@DISPATCH@@
  if(accepted){
@@ACCEPTED_NOTE@@
  }else if(frameSubmission.rejected){
@@NOFAULT@@
  }else{fault=true;}
 }
 json result=json::object();
@@RESULT_NEUTRAL@@
 const json& hostFrame=result;const bool submitNeutral=false;bool publishedSemanticNeutral=submitNeutral;
@@REPORT_NEUTRAL@@
 if(semanticNeutral!=publishedSemanticNeutral)throw std::runtime_error("reported output differs from actual full-neutral assembly");
 t_commonFrameSource=nullptr;return {accepted,publishedSemanticNeutral,frameSubmission.rejected,fault,receipt};
}
static InputCaptureGamepadSnapshot sample;
static void reset(){g_realStickTest={true,true};g_inputHostRequestedPersonaMatchesBound=true;logThrows=false;freshNeutralReady=true;freshNeutralCalls=0;now=100;power=1;g_inputHostVirtualIntentRevision=0;g_msiMainSourceRevision=1;g_inputHostVirtualIntentWriters=0;g_xinputTopologyEpoch=g_xinputSlotIdentityEpoch=1;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_msiDinputRouteDinputArmed=false;g_inputHostEpoch=7;g_inputHostProcess=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;g_inputHostTransportFault=exitFence=knownMsi=false;writerAllowed=msiEligible=true;logs.clear();sample={};sample.specialXbox=true;g_xinputSpecialButton=true;g_xinputConnectedMask=1;g_inputCaptureGamepadSnapshot={};t_commonFrameSource=nullptr;t_inputFrameSubmission=nullptr;pipeWrites=commandSequence=failuresFromHost=0;wire=json::object();connectHook=afterAssemblyHook={};connectFails=false;}
static json cases=json::array(),failures=json::array();static unsigned assertions=0;static std::string caseName;
static void require(bool value,const char* message){++assertions;if(!value)failures.push_back({{"case",caseName},{"message",message}});}
template<class F>static void run(const std::string& name,F fn){reset();caseName=name;const auto before=failures.size();const auto checks=assertions;fn();require(t_inputFrameSubmission==nullptr&&t_commonFrameSource==nullptr,"thread-local scopes unwind after every path");cases.push_back({{"name",name},{"passed",before==failures.size()},{"assertions",assertions-checks},{"pipeWrites",pipeWrites},{"commandSequence",commandSequence}});}
@@UNIFIED_CONTROLLER_INPUT@@
int main(){
 unifiedControllerInputCases();
 run("snapshot/Guide-press-is-captured",[]{g_xinputSpecialButton=true;captureReadSnapshot();require(g_inputCaptureGamepadSnapshot.specialXbox,"producer captures Guide together with physical snapshot");});
 run("snapshot/Guide-release-replaces-press",[]{g_inputCaptureGamepadSnapshot.specialXbox=true;g_xinputSpecialButton=false;captureReadSnapshot();require(!g_inputCaptureGamepadSnapshot.specialXbox,"producer clears Guide on a later complete snapshot");});
 for(const std::string route:{"xinput","msi-dinput","none"})run("diagnostic/actual-source/"+route,[&]{g_xinputConnectedMask=route=="xinput"?1:0;captureReadSnapshot(route,g_xinputConnectedMask);require(sourceDiagnostic().at("source")==route,"neutral admission reports captured source, never guesses ROG HID from zero XInput mask");});
 for(const bool fast:{false,true}){
  const std::string prefix=fast?"prepared/":"legacy/";
  for(const bool capturedGuide:{false,true})run(prefix+"same-generation-Guide/"+(capturedGuide?"press":"release"),[&]{
   sample.specialXbox=capturedGuide;g_xinputSpecialButton=!capturedGuide;
   const auto r=submit(&sample,false,fast);
   require(r.accepted&&!r.neutral&&pipeWrites==1,"same-generation complete snapshot remains publishable");
   require(wire["buttons"]==(capturedGuide?0x1401:0x1001),"Guide belongs to captured physical sample despite opposite later UI read");
   require(sample.readPowerGeneration==power&&sample.readTopologyGeneration==g_xinputTopologyEpoch&&sample.readIdentityGeneration==g_xinputSlotIdentityEpoch,"interleaving does not change any lifecycle generation");
  });
  for(const std::string route:{"xinput","xinput-any-connected","msi-dinput"})run(prefix+"valid-"+route,[&]{sample.inputSource=route;if(route=="msi-dinput"){knownMsi=true;g_msiDinputRouteDinputArmed=true;sample.connectedMask=0;}const auto r=submit(&sample,false,fast);require(r.accepted&&!r.neutral&&!r.fault&&pipeWrites==1,"current authorized source crosses actual guard/dispatch");require(wire["buttons"]==0x1401&&wire["backButtons"]==3,"normal content remains live");});
  const std::vector<std::pair<std::string,std::function<void()>>> stale={
   {"power",[]{++power;}},{"topology",[]{++g_xinputTopologyEpoch;}},{"identity",[]{++g_xinputSlotIdentityEpoch;}},
   {"age",[]{now=2000;}},{"unstable",[]{sample.readContextStable=false;}},{"no-sequence",[]{sample.readAttemptSequence=0;}},
   {"future-read",[]{sample.readCompletedTick=1000;}},{"unknown-route",[]{sample.inputSource="unknown";}},
   {"xinput-zero-mask",[]{sample.connectedMask=0;}},{"MSI-source-revoked",[]{knownMsi=true;g_msiDinputRouteDinputArmed=true;sample.inputSource="msi-dinput";sample.connectedMask=0;++g_msiMainSourceRevision;}}
  };
  for(const auto& row:stale)run(prefix+"initial-stale-neutral/"+row.first,[&]{row.second();const auto r=submit(&sample,false,fast);require(r.accepted&&r.neutral&&!r.fault&&pipeWrites==1,"already stale sample sends full neutral rather than stale live values");require(wire==focusIsolationAssembleFrame(focusIsolationNeutralContent()),"buttons,Guide,back,raw,triggers,IMU,touch and screen bits all use actual neutral assembly");});
  const std::vector<std::pair<std::string,std::function<void()>>> changes={
   {"target-persona-mismatch",[]{g_inputHostRequestedPersonaMatchesBound=false;}},{"target-disabled",[]{g_realStickTest.virtualTargetEnabled=false;}},{"capture-disabled",[]{g_realStickTest.enabled=false;}},{"power",[]{++power;}},{"power-revoked",[]{g_inputHostPowerAdmissionRevoked=true;}},{"writer-gate",[]{writerAllowed=false;}},
   {"Host",[]{++g_inputHostEpoch;}},{"intent",[]{++g_inputHostVirtualIntentRevision;}},{"intent-writing",[]{++g_inputHostVirtualIntentWriters;}},
   {"topology",[]{++g_xinputTopologyEpoch;}},{"identity",[]{++g_xinputSlotIdentityEpoch;}},{"read-expired",[]{now=2000;}}
  };
  for(const auto& row:changes)for(const bool duringConnect:{false,true})run(prefix+(duringConnect?"connect/":"assembled/")+row.first,[&]{if(duringConnect)connectHook=row.second;else afterAssemblyHook=row.second;const auto r=submit(&sample,false,fast);require(!r.accepted&&r.rejected&&!r.fault,"retired normal frame is a transient skip, not Host failure");require(pipeWrites==0&&commandSequence==0&&failuresFromHost==0,"pre-write rejection consumes no pipe request/sequence or backend recovery");require(g_inputHostActive,"retirement preserves target and accepted prior report ownership");require(r.receipt.is_object()&&r.receipt.value("deviceWriteAttempted",true)==false,"explicit receipt says no write attempt");});
  run(prefix+"MSI-revoke-during-connect",[&]{knownMsi=true;g_msiDinputRouteDinputArmed=true;sample.inputSource="msi-dinput";sample.connectedMask=0;connectHook=[]{++g_msiMainSourceRevision;};const auto r=submit(&sample,false,fast);require(r.rejected&&!r.fault&&pipeWrites==0,"old MSI source generation cannot be relabeled current");});
  run(prefix+"semantic-neutral-survives-source-loss",[&]{sample.live=false;sample.inputSource="none";sample.connectedMask=0;connectHook=[]{++g_xinputTopologyEpoch;++g_xinputSlotIdentityEpoch;};const auto r=submit(&sample,true,fast);require(r.accepted&&r.neutral&&pipeWrites==1,"same-target full neutral releases old input despite source absence/replacement");require(wire==focusIsolationAssembleFrame(focusIsolationNeutralContent()),"cleanup remains fully neutral");});
  run(prefix+"neutral-does-not-cross-target-replacement",[&]{connectHook=[]{++g_inputHostEpoch;};const auto r=submit(&sample,true,fast);require(r.rejected&&!r.fault&&pipeWrites==0,"neutral source exception cannot bypass original Host target");});
  run(prefix+"late-context-rejection-next-tick-releases-old-report",[&]{const auto first=submit(&sample,false,fast);require(first.accepted&&wire["buttons"]!=0,"a real old report was accepted first");connectHook=[]{++g_xinputTopologyEpoch;};const auto blocked=submit(&sample,false,fast);require(blocked.rejected&&!blocked.fault&&pipeWrites==1,"interleaved retirement does not write over prior accepted report");const auto cleanup=submit(&sample,false,fast);require(cleanup.accepted&&cleanup.neutral&&pipeWrites==2,"next tick sees retired snapshot and sends same-target neutral cleanup");require(wire==focusIsolationAssembleFrame(focusIsolationNeutralContent()),"prior pressed state is cleared even while no new canonical read arrives");});
  run(prefix+"fresh-next-read-resumes-without-rearm",[&]{afterAssemblyHook=[]{++g_xinputSlotIdentityEpoch;};const auto old=submit(&sample,false,fast);require(old.rejected&&!old.fault,"old read was skipped");sample.readIdentityGeneration=g_xinputSlotIdentityEpoch;sample.readAttemptSequence++;const auto fresh=submit(&sample,false,fast);require(fresh.accepted&&!fresh.neutral&&!fresh.fault&&pipeWrites==1,"next current sample resumes same target without recreate or retry counter");});
  run(prefix+"unbound-lifecycle-contract-preserved",[&]{const auto r=submit(nullptr,true,fast);require(r.accepted&&pipeWrites==1,"unbound lifecycle-neutral path preserves existing transport contract");});
  run(prefix+"stale-cleanup-does-not-fabricate-opening-neutral-evidence",[&]{freshNeutralReady=false;now=2000;const auto r=submit(&sample,false,fast);require(r.accepted&&r.neutral&&freshNeutralCalls==0,"full stale-source release bypasses opening wait without supplying fake physical-neutral samples");sample.readAttemptSequence++;sample.readStartedTick=sample.readCompletedTick=now;const auto fresh=submit(&sample,false,fast);require(!fresh.accepted&&freshNeutralCalls==1&&pipeWrites==1,"fresh live input still needs original physical-neutral admission");});
  run(prefix+"persistent-connect-rejection-is-event-bounded",[&]{const auto before=submit(&sample,false,fast);require(before.accepted,"normal frame establishes recovery logging baseline");const auto baselineLogs=logs.size();for(unsigned i=0;i<1000;++i){afterAssemblyHook=[]{++g_inputHostVirtualIntentRevision;};const auto r=submit(&sample,false,fast);require(r.rejected&&!r.fault,"each changed intent still rejects before pipe");}require(logs.size()-baselineLogs<=1,"a normal assembly cannot emit a fake recovery before every persistent pre-write rejection");const auto resumed=submit(&sample,false,fast);require(resumed.accepted&&logs.size()-baselineLogs<=2,"one actual accepted current frame emits at most one recovery transition");});
  run(prefix+"evidence-failure-never-admits-stale-live-or-breaks-cleanup",[&]{logThrows=true;now=2000;const auto r=submit(&sample,false,fast);require(r.accepted&&r.neutral&&pipeWrites==1,"throwing log sink does not change actual neutralization/admission");});
  run(prefix+"true-transport-failure-stays-fault",[&]{connectFails=true;const auto r=submit(&sample,false,fast);require(!r.accepted&&!r.rejected&&r.fault&&failuresFromHost==1,"a genuine current-context transport failure is not hidden as a transient skip");});
 }
 std::cout<<json({{"cases",cases},{"assertions",assertions},{"failures",failures},{"allTransportInert",true},{"realDeviceIo",false},{"wholeSubmitOrRequestCompiled",false}}).dump(2)<<std::endl;return failures.empty()?0:1;
}
