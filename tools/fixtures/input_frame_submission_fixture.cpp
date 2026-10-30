// Inert OS/transport boundaries. Named production blocks are inserted verbatim.
#define NOMINMAX
#include <windows.h>
#include <Xinput.h>
#include <wincrypt.h>
#include <charconv>
#include <cmath>
#include <limits>
#include <type_traits>
#define GetTickCount64 fixtureGetTickCount64
#include "json.hpp"
#include "controller_persona.h"
#include "canonical_frame_fingerprint_memo.h"
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
static struct{bool enabled=true,virtualTargetEnabled=true;std::string virtualTargetPersona="steamdeck";}g_realStickTest;
static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound{true};
using json=nlohmann::json;
static bool g_xinputSpecialButton=true;static DWORD g_xinputPrimarySlot=0,g_xinputConnectedMask=1;
static ULONGLONG now=100;static ULONGLONG GetTickCount64(){return now;}
static std::atomic<ULONGLONG> power{1},g_inputHostVirtualIntentRevision{0},g_msiMainSourceRevision{1},g_msiFeedbackModeGeneration{1};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0},g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1};
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false},g_msiDinputRouteDinputArmed{false};
static std::mutex g_inputHostPersonaMx;
static std::string g_inputHostBuildPersona,g_inputHostBoundPersona="steamdeck";
static std::string g_inputHostRunId="native-rog-submission-fixture",g_inputHostStartupNonce="native-rog-submission-fixture-nonce";
static unsigned long long g_inputHostBoundPowerGeneration=1,g_inputHostSequence=0,g_inputHostSampleSequence=0,g_inputHostConfigRevision=1;
static std::atomic<ULONGLONG>& g_powerGeneration=power;
static unsigned fixtureRequestId=0;
static std::string inputHostNewId(){return "native-fixture-request-"+std::to_string(++fixtureRequestId);}
static ULONGLONG g_inputHostEpoch=7;
@@PERSONA@@
@@ENVELOPE@@
static const char kC6aHexDigits[]="0123456789abcdef";
@@HASH@@
static ULONGLONG inputHostEpochToken(){return g_inputHostEpoch;}static ULONGLONG currentPowerGeneration(){return power;}
static bool g_inputHostProcess=true,g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostTransportFault=false,g_inputHostActive=true;
static bool exitFence=false,writerAllowed=true,knownMsi=false,msiEligible=true;
static bool inputHostExitFenceRaised(){return exitFence;}static bool sgVirtualWriterAllowedNow(){return writerAllowed;}static bool msiClawKnownBoardAuthority(){return knownMsi;}static bool msiDinputMainSourceEligible(){return msiEligible;}static bool msiControllerNeutralCleanupAllowed(bool neutral){return neutral&&knownMsi;}
static bool logThrows=false;static std::vector<json> logs;static void appendNativeLifecycleLog(const char*,const json& row){if(logThrows)throw std::runtime_error("inert evidence failure");logs.push_back(row);}
struct InputCaptureGamepadSnapshot {
    bool live=true;XINPUT_GAMEPAD pad{};DWORD primarySlot=0;unsigned connectedMask=1;std::string inputSource="xinput";
    ULONGLONG readAttemptSequence=1,readStartedTick=100,readCompletedTick=100,readPowerGeneration=1,readMsiSourceRevision=1,readMsiModeGeneration=1;
    unsigned readTopologyGeneration=1,readIdentityGeneration=1;bool readContextStable=true;
    unsigned physicalReaderPid=0;unsigned long long physicalReaderSequence=0;bool rogRawInputSubstitution=false;ymcc::touchhaptic::SourceToken feedbackSource{};bool specialXbox=false;
};
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
static ymcc::input::frameSubmission::ReadProgress g_inputHostReadProgress;
static void captureReadSnapshot(const std::string& inputSource="xinput",DWORD admittedMask=1){
 const bool live=true;const XINPUT_GAMEPAD pad{};const bool rogRawInputSubstitution=inputSource=="rog-hid-raw";
@@SNAPSHOT_GUIDE@@
}
static json sourceDiagnostic(){return json{
@@UI_SOURCE_FIELD@@
};}
static const InputCaptureGamepadSnapshot* t_commonFrameSource=nullptr;
@@COMMON_SOURCE_SCOPE@@
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
@@NORMALIZE_AXIS@@
@@FRAME@@
@@SERIALIZERS@@
enum class InputHostPublicationStage{SourceAbsent};static void inputHostNotePublicationStage(InputHostPublicationStage){}
static std::atomic<ULONGLONG> g_inputHostHealthySinceTick{0};
struct InputHostFastReceipt{};
static unsigned pipeWrites=0,commandSequence=0,failuresFromHost=0;static bool connectFails=false;
static bool freshNeutralReady=true;static unsigned freshNeutralCalls=0;
static bool inputHostFreshNeutralReady(bool,const XINPUT_GAMEPAD&,unsigned){++freshNeutralCalls;return freshNeutralReady;}
static unsigned g_xinputAdmittedMask=1;
static std::function<void()> connectHook,afterAssemblyHook;
static json wire=json::object(),pendingFrame,controlObservation;
static json issuedCommands=json::array();static bool exportIssued=false;
static std::string caseName,caseSource="xinput";
static bool request(const char* command,const json& payload,json* responseOut,
 const std::string* preparedText=nullptr,const std::string* preparedRequestId=nullptr,const unsigned long long* preparedSequence=nullptr){
@@PRE_ENVELOPE@@
 std::string requestText,requestIdValue;unsigned long long sequenceValue=0;
 if(preparedText){
@@PREPARED_ENVELOPE@@
 }else{
@@LEGACY_ENVELOPE@@
 }
 if(requestIdValue.empty()||!sequenceValue)throw std::runtime_error("invalid tuple");
 if(connectHook){auto hook=std::move(connectHook);connectHook={};hook();}
@@PRE_PIPE@@
 if(connectFails){++failuresFromHost;return false;}
 const auto issued=json::parse(requestText);
 for(const auto& item:controlObservation.items())if(issued.at(item.key())!=item.value())throw std::runtime_error("issued controls differ from actual frame");
 ++pipeWrites;++commandSequence;g_inputHostSequence=sequenceValue;wire=controlObservation;
 if(exportIssued)issuedCommands.push_back({{"case",caseName},{"source",t_commonFrameSource?t_commonFrameSource->inputSource:"none"},{"command",issued}});
 return true;
}
static bool inputHostRequestPreparedLocked(const char* command,const json& frame,const char*,int,json* responseOut,const std::string* text,const std::string* id,const unsigned long long* sequence,InputHostFastReceipt*){pendingFrame=frame;return request(command,frame,responseOut,text,id,sequence);}
static bool inputHostRequestLocked(const char* command,const json& frame,const char*,int,json* responseOut){pendingFrame=frame;return request(command,frame,responseOut);}
struct Result{bool accepted=false,neutral=false,rejected=false,fault=false;json receipt;};
static unsigned long long nativeSignalWireSample=0,nativeSignalWireTick=0;
static const char* inputCaptureMotionProofId(bool){return "pair-unproven-safe-zero";}
static Result submit(const InputCaptureGamepadSnapshot* sample,bool semanticNeutral=false,bool fast=true,const std::function<void()>& afterTicket={},bool sampleControls=false,bool physicalSourceAdmitted=true){
 CommonFrameSourceScope commonScope(sample);
@@SETUP@@
@@POWER@@
 if(!powerAdmitted){t_commonFrameSource=nullptr;return {};}
 if(afterTicket)afterTicket();
 bool suppressPublication=false;
@@NORMALIZE@@
 if(suppressPublication){t_commonFrameSource=nullptr;return {};}
 const bool msiNeutralCleanup=msiControllerNeutralCleanupAllowed(semanticNeutral);
 const XINPUT_GAMEPAD defaultPad{0x1001,127,255,1234,-4321,5678,-2345};
 const XINPUT_GAMEPAD pad=sampleControls&&sample?sample->pad:defaultPad;
@@GUIDE_ASSEMBLY@@
@@FRESH_WAIT@@
  t_commonFrameSource=nullptr;return {};
 }
 // Blend/calibration/motion outputs are fixture inputs; standard assignments are real.
 const int buttons=outputPad.wButtons;const short lxShort=outputPad.sThumbLX,lyShort=outputPad.sThumbLY,rx=outputPad.sThumbRX,ry=outputPad.sThumbRY;
 const auto axisOutputLt=pad.bLeftTrigger,axisOutputRt=pad.bRightTrigger;
 const float lt=static_cast<float>(axisOutputLt/255.0),rt=static_cast<float>(axisOutputRt/255.0),ltRaw=lt,rtRaw=rt;
 const bool imuAdmitted=!sampleControls,useMotion=!sampleControls;
 const double imuRawGx=12,imuRawGy=3,imuRawGz=0,maxa=0,may=0,maz=1,orientationW=1,orientationX=1,orientationY=0,orientationZ=0;
@@FRAME_CONTENT@@
 if(!sampleControls){frameContent.backButtons=3;frameContent.screenButtons=1;frameContent.touchpads.present=true;}
@@ASSEMBLY@@
 // The synthetic transport asserts canonical fields; materialize this test observation explicitly.
 controlObservation=materializeFrame();
 if(afterAssemblyHook){auto hook=std::move(afterAssemblyHook);afterAssemblyHook={};hook();}
@@HOST_READY@@
 bool accepted=false,fault=false;std::string lastRejectDetail;json receipt;
 if(hostFrameReady){
  const int effButtons=effectiveContent.buttons;
  const float effLx=inputHostNormalizeAxis(effectiveContent.lxShort),effLy=inputHostNormalizeAxis(effectiveContent.lyShort),effRx=inputHostNormalizeAxis(effectiveContent.rxShort),effRy=inputHostNormalizeAxis(effectiveContent.ryShort),effLt=effectiveContent.lt,effRt=effectiveContent.rt;
  const bool effUseMotion=useMotion&&!semanticNeutral;
  const bool frameTextReady=fast;const std::string frameRequestId=inputHostNewId();
  const unsigned long long frameSequence=g_inputHostSequence+1;
  const std::string frameRequestText=focusIsolationBuildTypedSubmitText(effectiveContent,frameRequestId,frameSequence,++g_inputHostSampleSequence,GetTickCount64(),inputHostCanonicalFrameHash(effButtons,effLx,effLy,effRx,effRy,effLt,effRt),inputCaptureMotionProofId(effUseMotion));
  InputHostFastReceipt fastReceipt;
@@LEGACY_PAYLOAD@@
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
static void reset(){g_inputHostReadProgress={};g_realStickTest={true,true};g_inputHostRequestedPersonaMatchesBound=true;logThrows=false;freshNeutralReady=true;freshNeutralCalls=0;now=100;power=1;g_inputHostVirtualIntentRevision=0;g_msiMainSourceRevision=1;g_inputHostVirtualIntentWriters=0;g_xinputTopologyEpoch=g_xinputSlotIdentityEpoch=1;g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_inputHostFaultHoldsStart=false;g_msiDinputRouteDinputArmed=false;g_inputHostEpoch=7;g_inputHostProcess=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;g_inputHostTransportFault=exitFence=knownMsi=false;writerAllowed=msiEligible=true;logs.clear();sample={};sample.inputSource=caseSource;sample.specialXbox=true;g_xinputSpecialButton=true;g_xinputConnectedMask=1;g_inputCaptureGamepadSnapshot={};t_commonFrameSource=nullptr;t_inputFrameSubmission=nullptr;pipeWrites=commandSequence=failuresFromHost=0;wire=json::object();controlObservation=json::object();connectHook=afterAssemblyHook={};connectFails=false;g_inputHostSequence=g_inputHostSampleSequence=0;fixtureRequestId=0;exportIssued=false;g_inputHostBoundPowerGeneration=1;g_inputHostBoundPersona="steamdeck";g_inputHostBuildPersona.clear();}
static json cases=json::array(),failures=json::array();static unsigned assertions=0;
static json pairedFrames=json::object();
static void require(bool value,const char* message){++assertions;if(!value)failures.push_back({{"case",caseName},{"message",message}});}
template<class F>static void run(const std::string& name,F fn){reset();caseName=name;const auto before=failures.size();const auto checks=assertions;fn();require(t_inputFrameSubmission==nullptr&&t_commonFrameSource==nullptr,"thread-local scopes unwind after every path");cases.push_back({{"name",name},{"passed",before==failures.size()},{"assertions",assertions-checks},{"pipeWrites",pipeWrites},{"commandSequence",commandSequence}});}
@@UNIFIED_CONTROLLER_INPUT@@
int main(){
 unifiedControllerInputCases();
 run("snapshot/Guide-press-is-captured",[]{g_xinputSpecialButton=true;captureReadSnapshot();require(g_inputCaptureGamepadSnapshot.specialXbox,"producer captures Guide together with physical snapshot");});
 run("snapshot/Guide-release-replaces-press",[]{g_inputCaptureGamepadSnapshot.specialXbox=true;g_xinputSpecialButton=false;captureReadSnapshot();require(!g_inputCaptureGamepadSnapshot.specialXbox,"producer clears Guide on a later complete snapshot");});
 for(const std::string route:{"xinput","msi-dinput","none"})run("diagnostic/actual-source/"+route,[&]{g_xinputConnectedMask=route=="xinput"?1:0;captureReadSnapshot(route,g_xinputConnectedMask);require(sourceDiagnostic().at("source")==route,"neutral admission reports captured source, never guesses ROG HID from zero XInput mask");});
 for(const bool fast:{false,true})for(const std::string source:{"xinput","rog-hid-raw"}){
  caseSource=source;
  const std::string mode=fast?"prepared/":"legacy/";
  const std::string prefix=mode+source+"/";
  run(prefix+"rog-compare/live-and-release",[&]{
   exportIssued=true;sample.specialXbox=false;sample.pad={0x1009,153,204,-32768,12345,32767,-23456};
   const auto binding=inputCaptureBindStandardInput(&sample);
   require(binding.known()&&binding.capabilityId()=="controller.input.xinput.standard","actual native wrapper gives either provenance original XInput capability");
   const bool requestedSteamDeck=inputHostConfiguredPersona()=="steamdeck";
   const auto held=submit(&sample,false,fast,{},true);
   require(held.accepted&&!held.neutral&&pipeWrites==1,"nonzero controls reach actual SUBMIT_FRAME dispatch");
   require(wire["buttons"]==0x1009&&wire["raw"]["buttons"]==0x1009,"A and up/right D-pad preserved");
   require(wire["raw"]["sThumbLX"]==-32768&&wire["raw"]["sThumbLY"]==12345&&wire["raw"]["sThumbRX"]==32767&&wire["raw"]["sThumbRY"]==-23456,"all four raw axes preserved");
   require(wire["raw"]["bLeftTrigger"]==153&&wire["raw"]["bRightTrigger"]==204&&wire["lt"]==static_cast<float>(153/255.0)&&wire["rt"]==static_cast<float>(204/255.0),"raw and normalized triggers preserved");
   require(wire["lx"]==-1.0f&&wire["rx"]==1.0f,"actual asymmetric normalization preserves signed endpoints");
   pairedFrames[mode+source+"/held"]=wire;
   sample.pad={};++sample.readAttemptSequence;
   const auto released=submit(&sample,false,fast,{},true);
   require(released.accepted&&!released.neutral&&pipeWrites==2,"advanced zero read produces release without retired-read cleanup");
   require(wire==focusIsolationAssembleFrame(focusIsolationNeutralContent()),"release clears all controls");
   pairedFrames[mode+source+"/release"]=wire;
   require(requestedSteamDeck&&inputHostConfiguredPersona()=="steamdeck"&&std::string(inputHostBoundOrCurrentPersona())=="steamdeck","requestedSteamDeck and bound target stay unchanged");
  });
  run(prefix+"rog-compare/intentional-YMCC-focus-shield",[&]{
   exportIssued=true;sample.specialXbox=true;sample.pad={0x1009,153,204,-32768,12345,32767,-23456};
   const auto shielded=submit(&sample,true,fast,{},true);
   require(shielded.accepted&&shielded.neutral&&wire==focusIsolationAssembleFrame(focusIsolationNeutralContent()),"intentional semantic focus shield retains full neutral submission");
   require(sample.pad.wButtons==0x1009&&sample.pad.bLeftTrigger==153,"shield retains original physical snapshot");
   ++sample.readAttemptSequence;sample.specialXbox=false;
   require(submit(&sample,false,fast,{},true).accepted&&wire["buttons"]==0x1009,"next fresh read resumes after shield");
  });
  run(prefix+"rog-compare/stale-and-unauthorized-live-rejected",[&]{
   exportIssued=true;sample.specialXbox=false;sample.pad={0x1009,153,204,-32768,12345,32767,-23456};
   sample.connectedMask=0;
   const auto denied=submit(&sample,false,fast,{},true);
   require(denied.accepted&&denied.neutral&&wire==focusIsolationAssembleFrame(focusIsolationNeutralContent()),"unauthorized live controls rejected; existing release cleanup permitted");
   sample.connectedMask=1;++sample.readAttemptSequence;now=2000;
   const auto stale=submit(&sample,false,fast,{},true);
   require(stale.accepted&&stale.neutral&&wire==focusIsolationAssembleFrame(focusIsolationNeutralContent()),"stale controls cannot cross actual frame gate");
  });
  run(prefix+"rog-compare/retired-before-dispatch",[&]{
   exportIssued=true;sample.specialXbox=false;sample.pad={0x1009,153,204,-32768,12345,32767,-23456};
   connectHook=[]{++g_xinputTopologyEpoch;};
   const auto retired=submit(&sample,false,fast,{},true);
   require(retired.rejected&&!retired.accepted&&!retired.fault&&pipeWrites==0,"source retirement before write issues no command");
   require(inputHostConfiguredPersona()=="steamdeck","retirement leaves requestedSteamDeck unchanged");
  });
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
  run(prefix+"late-context-rejection-next-tick-releases-old-report",[&]{const auto first=submit(&sample,false,fast);require(first.accepted&&wire["buttons"]!=0,"a real old report was accepted first");++sample.readAttemptSequence;connectHook=[]{++g_xinputTopologyEpoch;};const auto blocked=submit(&sample,false,fast);require(blocked.rejected&&!blocked.fault&&pipeWrites==1,"interleaved retirement does not write over prior accepted report");const auto cleanup=submit(&sample,false,fast);require(cleanup.accepted&&cleanup.neutral&&pipeWrites==2,"next tick sees retired snapshot and sends same-target neutral cleanup");require(wire==focusIsolationAssembleFrame(focusIsolationNeutralContent()),"prior pressed state is cleared even while no new canonical read arrives");});
  for(const std::string route:{"xinput","xinput-any-connected","msi-dinput"})run(prefix+"stalled-read-immediate-neutral/"+route,[&]{
   sample.inputSource=route;if(route=="msi-dinput"){knownMsi=true;g_msiDinputRouteDinputArmed=true;sample.connectedMask=0;}
   const auto first=submit(&sample,false,fast);require(first.accepted&&!first.neutral,"first valid canonical read remains admitted");
   const auto oldSequence=sample.readAttemptSequence;
   for(unsigned i=0;i<4;++i){const auto repeated=submit(&sample,false,fast);require(repeated.accepted&&repeated.neutral&&!repeated.fault,"repeated live read is neutral immediately even at age zero");require(wire==focusIsolationAssembleFrame(focusIsolationNeutralContent()),"all old controls released; no gyro/Guide/back/touch composition survives");}
   require(sample.readAttemptSequence==oldSequence&&sample.live,"neutralization must not forge producer/disconnect evidence");
   sample.readAttemptSequence++;const auto resumed=submit(&sample,false,fast);require(resumed.accepted&&!resumed.neutral&&!resumed.fault,"next actual API read resumes same target even when values and clock are identical");
  });
  run(prefix+"stalled-read-backwards-and-provider-revisit",[&]{
   sample.readAttemptSequence=50;require(submit(&sample,false,fast).accepted,"initial valid read");
   sample.readAttemptSequence=49;require(submit(&sample,false,fast).neutral,"backwards proof cannot replay old state");
   sample.inputSource="msi-dinput";sample.connectedMask=0;knownMsi=true;g_msiDinputRouteDinputArmed=true;sample.readAttemptSequence=7;require(!submit(&sample,false,fast).neutral,"separate valid provider has its own proof");
   sample.inputSource="xinput";sample.connectedMask=1;knownMsi=false;g_msiDinputRouteDinputArmed=false;sample.readAttemptSequence=50;require(submit(&sample,false,fast).neutral,"provider round trip cannot erase old XInput read history");
   sample.readAttemptSequence=51;require(!submit(&sample,false,fast).neutral,"fresh same-value API read restores output");
  });
  run(prefix+"invalid-generation-cannot-reset-read-history",[&]{
   sample.readAttemptSequence=80;require(!submit(&sample,false,fast).neutral,"initial valid read");
   sample.readPowerGeneration=2;sample.readAttemptSequence=1;require(submit(&sample,false,fast).neutral,"invalid power source is neutral");
   sample.readPowerGeneration=1;sample.readAttemptSequence=80;require(submit(&sample,false,fast).neutral,"invalid source cannot erase current read history");
   sample.readAttemptSequence=81;require(!submit(&sample,false,fast).neutral,"fresh valid read recovers without target rebuild");
  });
  run(prefix+"fresh-next-read-resumes-without-rearm",[&]{afterAssemblyHook=[]{++g_xinputSlotIdentityEpoch;};const auto old=submit(&sample,false,fast);require(old.rejected&&!old.fault,"old read was skipped");sample.readIdentityGeneration=g_xinputSlotIdentityEpoch;sample.readAttemptSequence++;const auto fresh=submit(&sample,false,fast);require(fresh.accepted&&!fresh.neutral&&!fresh.fault&&pipeWrites==1,"next current sample resumes same target without recreate or retry counter");});
  run(prefix+"unbound-lifecycle-contract-preserved",[&]{const auto r=submit(nullptr,true,fast);require(r.accepted&&pipeWrites==1,"unbound lifecycle-neutral path preserves existing transport contract");});
  run(prefix+"stale-cleanup-does-not-fabricate-opening-neutral-evidence",[&]{freshNeutralReady=false;now=2000;const auto r=submit(&sample,false,fast);require(r.accepted&&r.neutral&&freshNeutralCalls==0,"full stale-source release bypasses opening wait without supplying fake physical-neutral samples");sample.readAttemptSequence++;sample.readStartedTick=sample.readCompletedTick=now;const auto fresh=submit(&sample,false,fast);require(!fresh.accepted&&freshNeutralCalls==1&&pipeWrites==1,"fresh live input still needs original physical-neutral admission");});
  run(prefix+"persistent-connect-rejection-is-event-bounded",[&]{const auto before=submit(&sample,false,fast);require(before.accepted,"normal frame establishes recovery logging baseline");const auto baselineLogs=logs.size();for(unsigned i=0;i<1000;++i){++sample.readAttemptSequence;afterAssemblyHook=[]{++g_inputHostVirtualIntentRevision;};const auto r=submit(&sample,false,fast);require(r.rejected&&!r.fault,"each changed intent still rejects before pipe");}require(logs.size()-baselineLogs<=1,"a normal assembly cannot emit a fake recovery before every persistent pre-write rejection");++sample.readAttemptSequence;const auto resumed=submit(&sample,false,fast);require(resumed.accepted&&logs.size()-baselineLogs<=2,"one actual accepted current frame emits at most one recovery transition");});
  run(prefix+"evidence-failure-never-admits-stale-live-or-breaks-cleanup",[&]{logThrows=true;now=2000;const auto r=submit(&sample,false,fast);require(r.accepted&&r.neutral&&pipeWrites==1,"throwing log sink does not change actual neutralization/admission");});
  run(prefix+"true-transport-failure-stays-fault",[&]{connectFails=true;const auto r=submit(&sample,false,fast);require(!r.accepted&&!r.rejected&&r.fault&&failuresFromHost==1,"a genuine current-context transport failure is not hidden as a transient skip");});
 }
 caseSource="xinput";
 run("rog-compare/canonical-and-raw-controls-identical",[]{
  for(const std::string mode:{"prepared/","legacy/"})for(const std::string state:{"held","release"})
   require(pairedFrames.at(mode+"xinput/"+state)==pairedFrames.at(mode+"rog-hid-raw/"+state),"source provenance cannot change actual final frame controls");
  for(const auto& row:issuedCommands){const auto& command=row.at("command");
   require(command.at("command")=="SUBMIT_FRAME"&&command.at("persona")=="steamdeck"&&command.at("profileIdentity")=="steam-deck-composite","actual envelope preserves requested SteamDeck tuple");
   require(!command.contains("source")&&!command.contains("case"),"case/source metadata stays outside production envelope");
  }
 });
 std::cout<<json({{"cases",cases},{"assertions",assertions},{"failures",failures},{"issuedCommands",issuedCommands},{"allTransportInert",true},{"realDeviceIo",false},{"wholeSubmitOrRequestCompiled",false}}).dump(2)<<std::endl;return failures.empty()?0:1;
}
