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
// Explicit policy facts; no installed application or output device is used.
static struct{bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound{true};
using json=nlohmann::json;
struct XINPUT_GAMEPAD{std::uint16_t wButtons=0;BYTE bLeftTrigger=0,bRightTrigger=0;short sThumbLX=0,sThumbLY=0,sThumbRX=0,sThumbRY=0;};
namespace ymcc::screenpads{struct Snapshot{bool present=false;};inline json snapshotJson(const Snapshot&){return json::object();}}
@@SNAPSHOT@@
static thread_local const ymcc::legion_motion::Sample* t_legionMotionSubmission=nullptr;
static thread_local const ymcc::windows_motion::Stamp* t_windowsMotionSubmission=nullptr;
// Serial feedback remains an inert terminal outside frame/power acceptance.
struct InputFrameSubmission;
static void nativeSubmitGameFeedbackFrame(const InputFrameSubmission&,bool);
static void gamepadSerialRevokeGameFeedback() {}
@@SCOPE@@
static void nativeSubmitGameFeedbackFrame(const InputFrameSubmission&,bool) {}
static ULONGLONG tick=1000;static ULONGLONG GetTickCount64(){return tick;}
static thread_local std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0;
static bool g_inputHostPrepareMayHaveReachedHost=false;static unsigned targetCreateEntries=0;
// Non-frame target creation is outside this transport fixture; it must never succeed or run.
static bool inputHostTargetReturnBeforeCreateLocked(const char*,std::uint64_t){++targetCreateEntries;throw std::logic_error("target-create-outside-frame-fixture");}
@@COMMAND_DEADLINES@@
static unsigned long long power=1,g_inputHostEpoch=1;static std::atomic<unsigned long long> g_inputHostVirtualIntentRevision{0},g_msiMainSourceRevision{7},g_msiFeedbackModeGeneration{1};
static std::atomic<unsigned> g_inputHostVirtualIntentWriters{0},g_xinputTopologyEpoch{1};static std::atomic<unsigned> g_xinputSlotIdentityEpoch{1};
static std::atomic<bool> g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false};
static bool writerAllowed=true,exitFence=false,isMsi=false;
static HANDLE g_inputHostProcess=nullptr,g_inputHostServerPipe=nullptr;
static bool g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostTransportFault=false,g_inputHostPipeConnected=false;
static std::atomic<bool> g_msiDinputRouteDinputArmed{false},g_msiDinputRouteConfirmed{true},g_msiDinputSourceValid{true};
static struct{std::atomic<bool> enabled{true},mainInterfaceMatched{true};std::atomic<unsigned> productId{0x1902};}g_msiDinput;
static constexpr struct{unsigned pidDInput=0x1902;}kMsiClawCapability;
static unsigned long long currentPowerGeneration(){return power;}static unsigned long long inputHostEpochToken(){return g_inputHostEpoch;}
static bool msiClawKnownBoardAuthority(){return isMsi;}static bool sgVirtualWriterAllowedNow(){return writerAllowed;}static bool inputHostExitFenceRaised(){return exitFence;}
// Inert source-generation/lifecycle inputs for the verbatim production motion guard.
static std::atomic<unsigned long long> g_hidResolveCacheGeneration{1},g_legionMotionCaptureEpoch{1},g_legionMotionEpochStartedAt{1};
static std::atomic<bool> g_legionRawInputSubscribed{true},g_inputCaptureStop{false};
enum class PowerLifecycle{Ready,Stopped};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned selectedMotionSensor=0;static unsigned legionMotionSelectedSensorIndex() noexcept{return selectedMotionSensor;}
// Machine identity is an inert input; the model cache and final guard are production code.
enum class YmccFamily {Unknown,LenovoLegionGo};
static struct{YmccFamily family=YmccFamily::LenovoLegionGo;std::string deviceClass="HandheldCompanion.Devices.LegionGoTablet";}g_machineIdentity;
@@LEGION_BINDING@@
@@LEGION_CURRENT@@
@@SOURCE_ELIGIBLE@@
@@MSI_NEUTRAL@@
@@SOURCE_ADMISSION@@
@@ADAPTERS@@
static json logs=json::array();static void appendNativeLifecycleLog(const char* domain,const json& j){logs.push_back({{"domain",domain},{"row",j}});}
@@NOTE@@
@@REQUEST_GUARD@@
static float inputHostNormalizeAxis(short x){return x/32767.0f;}
@@FRAME@@
static HANDLE inputHostCreateServerPipeLocked(){auto h=fake::allocate(false,++fake::nextEndpoint);DWORD e=0;if(!ymcc::inputHost::pipeTransport::processRuntime().adopt(h,L"peer-submission",e))return nullptr;return h;}
static bool inputHostFrameSubmissionMayIssue(void*) noexcept;
static void inputHostFrameSubmissionTransportRejected() noexcept;
@@PIPE_WRAPPERS@@
static unsigned long long commandSequence=0;static unsigned faultBranches=0,newHosts=0,publicationNotes=0;static std::string lastRejectDetail;
static std::atomic<unsigned long long> g_inputHostHealthySinceTick{0};enum class InputHostPublicationStage{SourceAbsent};static void inputHostNotePublicationStage(InputHostPublicationStage){++publicationNotes;}
static bool preparedRequest(const char* command,json* responseOut){
@@PRE_ENVELOPE@@
 std::string requestText="synthetic-envelope";DWORD timeoutMs=1;const std::uint64_t sequenceValue=commandSequence+1;
@@TRANSPORT@@
 (void)writeElapsedUs;(void)readElapsedUs;
 if(!read){++faultBranches;return false;}++commandSequence;return true;
}
struct Result{bool sent=false,neutral=false,suppressed=false,contextRejected=false;json frame;};
static Result submit(const InputCaptureGamepadSnapshot* original,bool physicalSourceAdmitted,bool semanticNeutral=false,const std::function<void()>& betweenLocks={}){
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
 bool accepted=false;json receipt;if(hostFrameReady)accepted=preparedRequest("SUBMIT_FRAME",&receipt);
 if(accepted){}
 else if(frameSubmission.rejected){
@@NOFAULT@@
 }else if(hostFrameReady){++faultBranches;}
 r.sent=accepted;r.contextRejected=frameSubmission.rejected;return r;
}
static InputCaptureGamepadSnapshot makeSample(const std::string& source="xinput",DWORD mask=1){InputCaptureGamepadSnapshot s;s.live=true;s.inputSource=source;s.connectedMask=mask;s.readAttemptSequence=9;s.readStartedTick=tick-2;s.readCompletedTick=tick-1;s.readPowerGeneration=power;s.readTopologyGeneration=g_xinputTopologyEpoch;s.readIdentityGeneration=static_cast<unsigned>(g_xinputSlotIdentityEpoch.load());s.readContextStable=true;s.readMsiSourceRevision=g_msiMainSourceRevision;s.readMsiModeGeneration=g_msiFeedbackModeGeneration;return s;}
static void reset(){auto& rt=ymcc::inputHost::pipeTransport::processRuntime();if(g_inputHostServerPipe){fake::completeConnect=true;rt.close(g_inputHostServerPipe);rt.maintenance();g_inputHostServerPipe=nullptr;}fake::clear();g_inputHostProcess=reinterpret_cast<HANDLE>(std::uintptr_t(0x555));power=g_inputHostEpoch=1;g_inputHostSwitchRecoveryStartDeadline=0;g_inputHostPrepareMayHaveReachedHost=false;g_inputHostVirtualIntentRevision=0;g_inputHostVirtualIntentWriters=0;g_msiMainSourceRevision=7;g_msiFeedbackModeGeneration=1;t_legionMotionSubmission=nullptr;t_windowsMotionSubmission=nullptr;g_machineIdentity.deviceClass="HandheldCompanion.Devices.LegionGoTablet";g_hidResolveCacheGeneration=g_legionMotionCaptureEpoch=g_legionMotionEpochStartedAt=1;g_legionRawInputSubscribed=true;g_inputCaptureStop=false;g_powerLifecycle=PowerLifecycle::Ready;selectedMotionSensor=0;g_xinputTopologyEpoch=1;g_xinputSlotIdentityEpoch=1;g_inputReady=true;writerAllowed=true;exitFence=isMsi=false;g_inputHostPowerAdmissionRevoked=g_inputHostFaultHoldsStart=false;g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=g_inputHostPipeConnected=false;g_msiDinputRouteDinputArmed=false;g_msiDinputRouteConfirmed=g_msiDinputSourceValid=true;g_msiDinput.enabled=g_msiDinput.mainInterfaceMatched=true;g_msiDinput.productId=0x1902;tick=1000;commandSequence=0;faultBranches=newHosts=publicationNotes=0;logs=json::array();}
struct Checks{json cases=json::array(),failures=json::array();unsigned assertions=0;std::string name;void check(bool b,const char* why){++assertions;if(!b)failures.push_back({{"case",name},{"assertion",why}});}template<class F>void run(const char* n,F f){reset();name=n;const auto before=failures.size();try{f();}catch(const std::exception&e){check(false,e.what());}cases.push_back({{"name",n},{"passed",failures.size()==before},{"writes",fake::writes},{"connects",fake::connects},{"sequence",commandSequence},{"faultBranches",faultBranches},{"newHosts",newHosts},{"logs",logs}});}};
int main(){Checks c;
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
 reset();c.check(targetCreateEntries==0,"SUBMIT_FRAME never enters target-creation boundary");auto result=json{{"targetCreateEntries",targetCreateEntries},{"passed",c.failures.empty()},{"tests",c.cases.size()},{"assertions",c.assertions},{"failures",c.failures},{"cases",c.cases}};std::cout<<result.dump(2)<<'\n';return c.failures.empty()?0:1;}


