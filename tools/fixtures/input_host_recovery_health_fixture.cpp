// Inert native health adapter harness. Production blocks are inserted verbatim.
#include "json.hpp"
#include "input_host_recovery_health.h"
#include "sleep_recovery_diagnostics.h"
#include "input_frame_submission.h"
#include "legion_motion_source.h"
#include "windows_motion_submission.h"
#include "touch_haptic_dispatch.h"
#include "game_feedback_receipt.h"
#include <atomic>
#include <cstdint>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
using json=nlohmann::json;using ULONGLONG=unsigned long long;using InputHostMutex=std::mutex;
static ULONGLONG fakeNow=100;static ULONGLONG GetTickCount64(){return fakeNow;}
static std::atomic<unsigned long long> fakePower{1},g_inputHostPresentationWorkGeneration{1},g_wakeKeptTargetGeneration{0};
static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1};
static std::atomic<unsigned long long> g_msiMainSourceRevision{1};
static auto currentPowerGeneration(){return fakePower.load();}
struct InputCaptureGamepadSnapshot {bool live=true;unsigned connectedMask=1;std::string inputSource="xinput";unsigned long long readAttemptSequence=1,readPowerGeneration=1;ULONGLONG readStartedTick=100,readCompletedTick=100;unsigned readTopologyGeneration=1,readIdentityGeneration=1;bool readContextStable=true;unsigned long long readContinuityGeneration=0,readMsiSourceRevision=1;unsigned primarySlot=0;};
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;static const InputCaptureGamepadSnapshot* t_commonFrameSource=nullptr;static std::mutex g_inputCaptureGamepadSnapshotMx;
static std::mutex g_inputHostMx;static std::atomic<ULONGLONG> g_inputHostHealthySinceTick{0};
static ymcc::input::recoveryHealth::Window g_inputHostRecoveryHealth;static bool g_inputHostRecoveryHealthWasTracking=false;
static constexpr ULONGLONG kRecoveryHealthyCompleteMs=2000,kRecoveryCooldownMs=1000;
static std::atomic<bool> g_inputHostRecoveryPending{false},g_inputHostRecoveryTerminalLogged{false},g_inputHostRecoveryIntentReset{false},g_inputHostTupleHealPending{false};
static std::atomic<int> g_inputHostRecoveryEpisodeAttempts{2},g_inputHostRecoveryFallbackState{0};
static std::atomic<ULONGLONG> g_inputHostRecoveryDeferredUntil{0},g_inputHostRecoveryLastStartTick{0};
static unsigned long long g_inputHostEpoch=7,g_inputHostBoundPowerGeneration=1;static bool g_inputHostProcess=true,g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostTransportFault=false;
static std::atomic<bool> g_inputHostFaultHoldsStart{false},g_inputHostPowerAdmissionRevoked{false},g_externalPriorityVirtualSuspended{false},g_inputReady{true};
struct {bool enabled=true,virtualTargetEnabled=true;}g_realStickTest;
static std::atomic<bool> g_inputHostRequestedPersonaMatchesBound{true};
enum class PowerLifecycle{Ready,Suspended};static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static int personaCopies=0;static std::string configuredPersona="elite",boundPersona="elite";static const char* inputHostPersona(){return configuredPersona.c_str();}static std::string inputHostBoundPersonaValue(){personaCopies++;return boundPersona;}
static bool exitFence=false,writerAllowed=true,releaseWindow=false;static bool inputHostExitFenceRaised(){return exitFence;}static bool sgVirtualWriterAllowedNow(){return writerAllowed;}static bool physicalReleaseWindowActive(){return releaseWindow;}
static bool fakeMsiBoard=true,fakeMsiEligible=true;static bool msiClawKnownBoardAuthority(){return fakeMsiBoard;}static std::atomic<bool> g_msiDinputRouteDinputArmed{false};static bool msiDinputMainSourceEligible(){return fakeMsiEligible;}static bool msiControllerRepairScope(){return g_msiDinputRouteDinputArmed.load()&&msiClawKnownBoardAuthority();}static bool msiControllerNeutralCleanupAllowed(bool semanticNeutral){return semanticNeutral&&msiClawKnownBoardAuthority();}
namespace ymcc::screenpads {static void observeNativeTransport(unsigned long long,bool){}}
static int queueCalls=0,ackCalls=0;static std::vector<json> logs;static void appendNativeLifecycleLog(const char*,const json& row){logs.push_back(row);}static bool inputHostQueueLifecycleWork(const char*){queueCalls++;return true;}
enum class InputHostRecoveryAdmission{Allowed,DeferredPower,Cancelled,BudgetExhausted};static InputHostRecoveryAdmission inputHostRecoveryAdmissionLocked(){return InputHostRecoveryAdmission::Allowed;}
static bool g_inputHostActive=true;static std::string g_inputHostLifecycle="ready";
// Downstream serial feedback is outside health settlement; no output worker runs.
static void gamepadSerialRevokeGameFeedback() {}
@@FRAME_CONTEXT@@
// Feedback dispatch is an inert downstream terminal; health tests do not drive motors.
static void nativeSubmitGameFeedbackFrame(const InputFrameSubmission&,bool) {}
// The health cases supply no motion. Fail closed if a Legion sample is accidentally attached.
static bool legionMotionSubmissionCurrent(const ymcc::legion_motion::Sample&,ULONGLONG) noexcept {return false;}
@@SOURCE_GATE@@
@@HELPERS@@
@@MAINTENANCE@@
static json frame(bool semanticNeutral=false,bool physicalSourceAdmitted=true,const std::function<void()>& afterCapture={}){
 const auto held=g_inputCaptureGamepadSnapshot;t_commonFrameSource=&held;
 @@FRAME_LOCAL@@
 @@SUPPRESS@@
 if(suppressPublication){t_commonFrameSource=nullptr;return {};}
 std::lock_guard<std::mutex> hostLock(g_inputHostMx);
 @@REQUEST@@
 if(afterCapture)afterCapture();ackCalls++;
 @@ACK@@
 t_commonFrameSource=nullptr;return {};
}
static unsigned long long readSequence=1;
static void source(ULONGLONG time,bool live=true,const char* route="xinput"){fakeNow=time;
 @@PRODUCER_PREVIOUS@@
g_inputCaptureGamepadSnapshot={live,live?1u:0u,live?route:"none",++readSequence,fakePower.load(),time,time,g_xinputTopologyEpoch.load(),g_xinputSlotIdentityEpoch.load(),true};
 g_inputCaptureGamepadSnapshot.readMsiSourceRevision=g_msiMainSourceRevision.load(std::memory_order_acquire);
 @@PRODUCER_COMMIT@@
}
static void reset(){g_msiMainSourceRevision=1;fakeMsiBoard=true;fakeMsiEligible=true;g_msiDinputRouteDinputArmed=false;fakeNow=100;fakePower=1;g_xinputTopologyEpoch=1;g_xinputSlotIdentityEpoch=1;g_inputHostPresentationWorkGeneration=1;g_inputHostEpoch=7;g_inputHostBoundPowerGeneration=1;g_inputHostProcess=g_inputHostPrepared=g_inputHostNeutralized=true;g_inputHostTransportFault=false;g_inputHostFaultHoldsStart=false;g_inputHostPowerAdmissionRevoked=false;g_externalPriorityVirtualSuspended=false;g_inputReady=true;g_realStickTest={true,true};g_powerLifecycle=PowerLifecycle::Ready;configuredPersona=boundPersona="elite";exitFence=false;writerAllowed=true;releaseWindow=false;g_inputHostRecoveryPending=false;g_inputHostTupleHealPending=false;g_inputHostRecoveryEpisodeAttempts=2;g_inputHostRecoveryTerminalLogged=true;g_inputHostRecoveryIntentReset=false;g_inputHostRecoveryFallbackState=0;g_inputHostRecoveryDeferredUntil=0;g_inputHostRecoveryLastStartTick=0;g_inputHostHealthySinceTick=0;g_inputHostRecoveryHealth.clear();g_inputHostRecoveryHealthWasTracking=false;personaCopies=0;queueCalls=ackCalls=0;logs.clear();source(100);}
static json cases=json::array(),checks=json::array();static void expect(const char* name,json actual,json expected){checks.push_back({{"name",name},{"actual",actual},{"expected",expected},{"passed",actual==expected}});}static void finish(const std::string& name,bool witness=false){bool pass=true;for(const auto& c:checks)pass=pass&&c["passed"].get<bool>();cases.push_back({{"name",name},{"regressionWitness",witness},{"passed",pass},{"checks",checks},{"healthySince",g_inputHostHealthySinceTick.load()},{"attempts",g_inputHostRecoveryEpisodeAttempts.load()},{"ackCalls",ackCalls}});checks=json::array();}
int main(){
 reset();frame();source(150,false);frame(false,false);fakeNow=3000;inputHostRecoveryMaintenanceTick();expect("source absence clears health",g_inputHostHealthySinceTick.load(),0);expect("single ACK then disconnect does not refund budget",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("single-ACK-source-none-no-new-ACK",true);
 reset();frame();fakeNow=8000;inputHostRecoveryMaintenanceTick();expect("time alone does not refund budget",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("one-ACK-wall-clock-only",true);
 reset();frame();source(150);frame(false,true,[]{fakeNow=8000;});inputHostRecoveryMaintenanceTick();expect("delayed response covers read time not waiting time",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("long-ACK-source-read-horizon",true);
 reset();frame();source(3000,false);frame(true,false);inputHostRecoveryMaintenanceTick();expect("semantic cleanup still sent",ackCalls,2);expect("source-none neutral cannot keep old health",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("source-none-neutral-cleanup",true);
 for(const std::string bad:{"power","topology","identity","unstable","missing-read"}){
  reset();frame();source(3000);if(bad=="power")g_inputCaptureGamepadSnapshot.readPowerGeneration=99;if(bad=="topology")g_inputCaptureGamepadSnapshot.readTopologyGeneration=99;if(bad=="identity")g_inputCaptureGamepadSnapshot.readIdentityGeneration=99;if(bad=="unstable")g_inputCaptureGamepadSnapshot.readContextStable=false;if(bad=="missing-read")g_inputCaptureGamepadSnapshot.readAttemptSequence=0;frame();inputHostRecoveryMaintenanceTick();expect("stale/unknown real-source proof cannot refund budget",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("stale-source-"+bad,true);
 }
 for(const std::string changed:{"host","power","intent","topology","identity"}){
  reset();frame();if(changed=="host")g_inputHostEpoch++;if(changed=="power"){fakePower++;g_inputHostBoundPowerGeneration=fakePower.load();}if(changed=="intent")g_inputHostPresentationWorkGeneration++;if(changed=="topology")g_xinputTopologyEpoch++;if(changed=="identity")g_xinputSlotIdentityEpoch++;source(3000);frame();inputHostRecoveryMaintenanceTick();expect("new generation restarts rather than inherits old timer",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("new-context-"+changed,true);
 }
 for(const std::string changed:{"off","persona","power-revoked","host-not-ready"}){
  reset();frame();source(3000);if(changed=="off")g_realStickTest.virtualTargetEnabled=false;if(changed=="persona")configuredPersona="xbox360";if(changed=="power-revoked")g_inputHostPowerAdmissionRevoked=true;if(changed=="host-not-ready")g_inputHostPrepared=false;inputHostRecoveryMaintenanceTick();expect("current target/power/Host gate blocks completion",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("maintenance-context-"+changed,true);
 }
 // Normal source readings may have identical buttons/payload forever. No
 // comparison with a prior input hash or key press is used for health.
 reset();frame();source(1100);frame();source(2200);frame();inputHostRecoveryMaintenanceTick();expect("unchanged real input with receipts completes",g_inputHostRecoveryEpisodeAttempts.load(),0);finish("idle-same-payload-valid-read-and-ACK");
 reset();frame();source(150,false);frame(false,false);source(4000);frame();inputHostRecoveryMaintenanceTick();expect("source recovery cannot inherit old elapsed window",g_inputHostRecoveryEpisodeAttempts.load(),2);source(4500);source(5100);source(5700);source(6100);frame();inputHostRecoveryMaintenanceTick();expect("returned source accumulates fresh health",g_inputHostRecoveryEpisodeAttempts.load(),0);finish("source-return-new-window",true);
 reset();frame();source(700);source(1300);source(1900);source(2300);inputHostRecoveryMaintenanceTick();expect("dedupe is not source loss",g_inputHostHealthySinceTick.load()!=0,true);expect("dedupe does not manufacture a newer receipt horizon",g_inputHostRecoveryEpisodeAttempts.load(),2);frame();inputHostRecoveryMaintenanceTick();expect("sparse fresh receipt may close without frequency threshold",g_inputHostRecoveryEpisodeAttempts.load(),0);finish("dedup-or-sparse-ACK-no-disconnect",true);
 reset();frame();fakeNow=5000;frame();inputHostRecoveryMaintenanceTick();expect("reused source snapshot cannot extend coverage",g_inputHostRecoveryEpisodeAttempts.load(),2);expect("expired reader evidence clears health",g_inputHostHealthySinceTick.load(),0);finish("reused-read-no-fabricated-progress",true);cases.back()["readerGapWitness"]=true;
 reset();frame();fakeNow=1800;inputHostRecoveryMaintenanceTick();expect("cached live snapshot expires without a live=false write",g_inputHostHealthySinceTick.load(),0);finish("stale-cached-live-source",true);cases.back()["readerGapWitness"]=true;
 reset();frame();fakeNow=3000;frame();inputHostRecoveryMaintenanceTick();expect("ACK cannot refresh cached source",g_inputHostHealthySinceTick.load(),0);finish("stale-cached-source-with-new-ACK",true);cases.back()["readerGapWitness"]=true;
 reset();frame();source(4100);frame();inputHostRecoveryMaintenanceTick();expect("long reader stop is not healthy coverage",g_inputHostRecoveryEpisodeAttempts.load(),2);source(5100);frame();source(6200);frame();inputHostRecoveryMaintenanceTick();expect("fresh reads after restart can later close",g_inputHostRecoveryEpisodeAttempts.load(),0);finish("reader-long-stop-and-recovery",true);cases.back()["readerGapWitness"]=true;
 reset();frame();source(4200);g_inputCaptureGamepadSnapshot.readStartedTick=100;frame();inputHostRecoveryMaintenanceTick();expect("long canonical call cannot appear fresh at its end",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("long-blocked-canonical-read",true);cases.back()["readerGapWitness"]=true;
 reset();frame();g_inputHostHealthySinceTick=0;source(3000);frame();inputHostRecoveryMaintenanceTick();expect("external fault reset restarts pure state",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("existing-fault-reset-synchronizes-window",true);
 reset();frame();source(3000);frame(false,true,[]{g_inputHostPowerAdmissionRevoked=true;});inputHostRecoveryMaintenanceTick();expect("power revocation during ACK cannot complete",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("power-revoked-during-receipt",true);
 reset();source(200);frame(true,true);fakeNow=5000;inputHostRecoveryMaintenanceTick();expect("neutral cleanup is not faulted",ackCalls,1);expect("semantic neutral cannot start health",g_inputHostHealthySinceTick.load(),0);expect("semantic neutral does not refund budget",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("semantic-neutral-remains-deliverable");
 reset();frame();source(2200);frame();g_inputHostRecoveryPending=true;inputHostRecoveryMaintenanceTick();expect("pending recovery cannot close budget",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("pending-recovery-retains-budget");
 reset();frame();source(2200);frame();g_inputHostTupleHealPending=true;inputHostRecoveryMaintenanceTick();expect("pending tuple heal cannot close budget",g_inputHostRecoveryEpisodeAttempts.load(),2);finish("pending-tuple-retains-budget");

 reset();g_msiDinputRouteDinputArmed=true;
 for(const ULONGLONG t:{100ULL,1100ULL,2200ULL}){source(t,true,"msi-dinput");g_inputCaptureGamepadSnapshot.connectedMask=0;
#if HEALTH_FIXED
 expect("authorized MSI canonical route allows zero slot mask",inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true).live,true);
#endif
 frame();}
 inputHostRecoveryMaintenanceTick();expect("legal MSI zero-mask reads and receipts can close",g_inputHostRecoveryEpisodeAttempts.load(),0);finish("MSI-canonical-zero-slot-mask");cases.back()["candidateGapWitness"]=true;
 reset();g_msiDinputRouteDinputArmed=true;source(100,true,"msi-dinput");frame();source(1100,true,"msi-dinput");frame();g_msiMainSourceRevision=2;source(2100,true,"msi-dinput");frame();inputHostRecoveryMaintenanceTick();expect("new MSI reader cannot settle old recovery budget",g_inputHostRecoveryEpisodeAttempts.load(),2);source(3100,true,"msi-dinput");frame();source(4100,true,"msi-dinput");frame();inputHostRecoveryMaintenanceTick();expect("new MSI reader fresh accepted window can settle",g_inputHostRecoveryEpisodeAttempts.load(),0);json healthyReceipt=json::object();for(const auto& row:logs)if(row.contains("proofScope"))healthyReceipt=row;expect("ordinary healthy receipt names actual reader",healthyReceipt.value("sourceRoute",std::string{}),"msi-dinput");expect("ordinary healthy receipt binds new source revision",healthyReceipt.value("sourceIncarnation",0ULL),2);expect("ordinary healthy receipt keeps continuity proof",healthyReceipt.value("sourceContinuity",0ULL)!=0,true);finish("MSI-republish-cannot-settle-old-episode");
 for(const std::string kind:{"MSI-unauthorized-board","MSI-route-not-armed","MSI-main-not-eligible","XInput-zero-mask","virtual-loop-zero-mask","unknown-source-name","real-source-none"}){
  reset();source(100);g_inputCaptureGamepadSnapshot.inputSource="msi-dinput";g_inputCaptureGamepadSnapshot.connectedMask=0;g_msiDinputRouteDinputArmed=true;
  if(kind=="MSI-unauthorized-board")fakeMsiBoard=false;
  if(kind=="MSI-route-not-armed")g_msiDinputRouteDinputArmed=false;
  if(kind=="MSI-main-not-eligible")fakeMsiEligible=false;
  if(kind=="XInput-zero-mask"||kind=="virtual-loop-zero-mask")g_inputCaptureGamepadSnapshot.inputSource="xinput";
  if(kind=="unknown-source-name"){g_inputCaptureGamepadSnapshot.inputSource="unknown";g_inputCaptureGamepadSnapshot.connectedMask=1;}
  if(kind=="real-source-none"){g_inputCaptureGamepadSnapshot.inputSource="none";g_inputCaptureGamepadSnapshot.live=false;}
#if HEALTH_FIXED
  expect("route/authority/mask contract excludes unproven source",inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true).live,false);
#else
  expect("pre-helper baseline lacks this projection contract",true,true);
#endif
  finish(kind);cases.back()["candidateGapWitness"]=true;
 }
 for(const std::string family:{"XInput","ROG","GPD","legacy-XInput-fallback"}){
  reset();fakeMsiBoard=false;for(const ULONGLONG t:{100ULL,1100ULL,2200ULL}){source(t,true,family=="legacy-XInput-fallback"?"xinput-any-connected":"xinput");frame();}inputHostRecoveryMaintenanceTick();expect("normal non-MSI canonical source remains accepted",g_inputHostRecoveryEpisodeAttempts.load(),0);finish(family+"-normal-canonical");if(family=="legacy-XInput-fallback")cases.back()["candidateGapWitness"]=true;
 }

 reset();g_inputHostRecoveryEpisodeAttempts=0;g_inputHostRecoveryTerminalLogged=false;frame();source(700);source(1300);source(1900);source(2300);inputHostRecoveryMaintenanceTick();frame();expect("ordinary path adds no persona copies",personaCopies,0);expect("normal T1 timestamp remains available",g_inputHostHealthySinceTick.load()!=0,true);finish("no-recovery-lightweight-dedupe");cases.back()["candidateGapWitness"]=true;
 for(const bool terminal:{false,true}){
  reset();g_inputHostRecoveryEpisodeAttempts=0;g_inputHostRecoveryTerminalLogged=false;frame();source(700);source(1300);source(1900);source(2300);g_inputHostRecoveryEpisodeAttempts=terminal?0:1;g_inputHostRecoveryTerminalLogged=terminal;frame();expect("first recovery receipt establishes current source window",g_inputHostHealthySinceTick.load(),2300);expect("old normal timer cannot refund new episode",g_inputHostRecoveryEpisodeAttempts.load(),terminal?0:1);expect("tracking performs current persona verification",personaCopies>0,true);finish(terminal?"terminal-first-frame-tracks":"episode-first-frame-tracks",true);cases.back()["candidateGapWitness"]=true;
 }
 int failed=0,count=0;for(const auto& c:cases){if(!c["passed"].get<bool>())failed++;count+=static_cast<int>(c["checks"].size());}std::cout<<json{{"cases",cases},{"caseCount",cases.size()},{"checks",count},{"failedCases",failed},{"deviceIO",false},{"newThreads",false}}.dump(2)<<std::endl;return failed?1:0;
}
