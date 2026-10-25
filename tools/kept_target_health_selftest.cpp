#define NOMINMAX
#include <Windows.h>
#include <Xinput.h>
#include <atomic>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>
#include "json.hpp"
#include "input_host_recovery_health.h"
#include "sleep_recovery_diagnostics.h"
#include "msi_virtual_report_diagnostic.h"
using json=nlohmann::json;
@@ENUMS@@
@@SNAPSHOT@@
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<unsigned long long> g_powerGeneration{1},g_wakeKeptTargetGeneration{0},g_inputHostPresentationWorkGeneration{1},g_msiMainSourceRevision{1};
static std::atomic<unsigned> g_xinputTopologyEpoch{1},g_xinputSlotIdentityEpoch{1};
static std::atomic<bool> g_externalPriorityVirtualSuspended{false},g_inputReady{true},g_inputHostPowerAdmissionRevoked{false},g_inputHostFaultHoldsStart{false},g_inputHostRecoveryIntentReset{false};
static std::atomic<bool> g_inputHostRecoveryTerminalLogged{false},g_inputHostRecoveryPending{false},g_inputHostTupleHealPending{false};
static std::atomic<unsigned> g_inputHostRecoveryEpisodeAttempts{0};
static unsigned long long g_inputHostEpoch=7,g_inputHostBoundPowerGeneration=1;
static bool g_inputHostProcess=true,g_inputHostPrepared=true,g_inputHostNeutralized=true,g_inputHostActive=true,g_inputHostTransportFault=false;
static struct {bool enabled=true,virtualTargetEnabled=true;} g_realStickTest;
static struct {YmccFamily family=YmccFamily::MsiClaw;}g_machineIdentity;
static bool aoac=false,exitFence=false,releaseWindow=false,personaTransition=false,slotBusy=false;
static std::string persona="steamdeck",boundPersona="steamdeck";
static ULONGLONG now=100;
static ULONGLONG fixtureTick(){return now;}
#define GetTickCount64 fixtureTick
static ULONGLONG currentPowerGeneration(){return g_powerGeneration.load();}
static bool sgPlatformIsAlwaysOnAlwaysConnected(){return aoac;}
static bool inputHostExitFenceRaised(){return exitFence;}
static bool physicalReleaseWindowActive(){return releaseWindow;}
static const char* inputHostPersona(){return persona.c_str();}
static std::string inputHostBoundPersonaValue(){return boundPersona;}
static bool inputHostPersonaTransitionPending(){return personaTransition;}
static bool ymccSlotHandoffPlanBusy(){return slotBusy;}
static bool msiControllerRepairScope(){return g_machineIdentity.family==YmccFamily::MsiClaw;}
static bool msiDinputMainSourceEligible(){return true;}
static std::atomic<bool> g_physicalReleaseWindowDutyActive{false},g_wakeHoldPhysicalUnhide{false},g_wakeRescanRequested{false};
static constexpr ULONGLONG kT1HealthyAdmitMs=250;
static std::atomic<ULONGLONG> g_inputHostHealthySinceTick{0};
static ymcc::input::recoveryHealth::Window g_inputHostRecoveryHealth;
static bool g_inputHostRecoveryHealthWasTracking=false;
static InputCaptureGamepadSnapshot g_inputCaptureGamepadSnapshot;
static std::mutex g_inputCaptureGamepadSnapshotMx;
static bool inputHostProcessAliveSnapshot(){return g_inputHostProcess;}
static unsigned keptLogs=0,psRearmCalls=0;
static void appendNativeLifecycleLog(const char*,const json&){++keptLogs;}
static void inputHostRequestPsWakeRearm(unsigned long long,const char*){++psRearmCalls;}
@@KEEP@@
@@WRITER@@
@@PROTOCOL@@
@@HELPERS@@
@@T1@@
static void keptWakeBranch(){const bool hidRefreshed=true;
@@KEPT@@
}
static unsigned long long readSequence=0;
static std::string readRoute="xinput";
static void reset(){g_powerGeneration=1;g_wakeKeptTargetGeneration=0;g_inputHostPresentationWorkGeneration=1;g_msiMainSourceRevision=1;g_xinputTopologyEpoch=g_xinputSlotIdentityEpoch=1;g_powerLifecycle=PowerLifecycle::Ready;g_externalPriorityVirtualSuspended=false;g_inputReady=true;g_inputHostPowerAdmissionRevoked=g_inputHostFaultHoldsStart=g_inputHostRecoveryIntentReset=false;g_inputHostRecoveryTerminalLogged=g_inputHostRecoveryPending=g_inputHostTupleHealPending=false;g_inputHostRecoveryEpisodeAttempts=0;g_inputHostEpoch=7;g_inputHostBoundPowerGeneration=1;g_inputHostProcess=g_inputHostPrepared=g_inputHostNeutralized=g_inputHostActive=true;g_inputHostTransportFault=false;g_realStickTest={true,true};g_machineIdentity.family=YmccFamily::MsiClaw;aoac=exitFence=releaseWindow=personaTransition=slotBusy=false;persona=boundPersona="steamdeck";g_physicalReleaseWindowDutyActive=false;g_wakeHoldPhysicalUnhide=g_wakeRescanRequested=false;g_inputHostHealthySinceTick=0;g_inputHostRecoveryHealth.clear();g_inputHostRecoveryHealthWasTracking=false;g_inputCaptureGamepadSnapshot={};readSequence=0;readRoute="xinput";keptLogs=psRearmCalls=0;now=100;}
// Canonical read and validated ACK event are explicit test boundaries, not device/pipe operations.
static auto read(ULONGLONG at){now=at;auto previous=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);auto& s=g_inputCaptureGamepadSnapshot;s={};s.live=true;s.connectedMask=readRoute=="msi-dinput"?0:1;s.inputSource=readRoute;s.readMsiSourceRevision=g_msiMainSourceRevision;s.readAttemptSequence=++readSequence;s.readPowerGeneration=g_powerGeneration;s.readStartedTick=s.readCompletedTick=now;s.readTopologyGeneration=g_xinputTopologyEpoch;s.readIdentityGeneration=g_xinputSlotIdentityEpoch;s.readContextStable=true;s.readContinuityGeneration=ymcc::input::recoveryHealth::nextContinuity(previous,inputHostRecoveryHealthSource(&s,true),now);return inputHostRecoveryHealthSource(&s,true);}
static void ack(ULONGLONG at){const auto source=read(at);const auto request=inputHostRecoveryHealthContextLocked(false);inputHostRecoveryHealthAcceptedLocked(request,source,true);}
static std::string t1(){const auto* reason=physicalReleaseWindowAdmissionBlocked();return reason?reason:"allowed";}
static std::vector<std::string> cases,failures;static unsigned assertions=0;static json trace=json::array();
static void check(bool ok,const char* text){++assertions;if(!ok)failures.push_back(cases.back()+": "+text);}
template<class F>static void run(const std::string& name,F fn){reset();cases.push_back(name);fn();trace.push_back({{"case",name},{"boundProtocolPower",inputHostProtocolPowerGenerationLocked()},{"g_powerGeneration",g_powerGeneration.load()},{"keptPower",g_wakeKeptTargetGeneration.load()},{"attempts",g_inputHostRecoveryEpisodeAttempts.load()},{"healthySince",g_inputHostHealthySinceTick.load()},{"t1Gate",t1()}});}
static void retainedWake(){g_powerLifecycle=PowerLifecycle::Suspending;++g_powerGeneration;g_powerLifecycle=PowerLifecycle::Resuming;g_powerLifecycle=PowerLifecycle::Ready;keptWakeBranch();}
int main(){
 run("same-g_powerGeneration-control",[]{ack(100);ack(400);check(g_inputHostHealthySinceTick==100,"same-g_powerGeneration ACK builds health");check(t1()=="allowed","same-g_powerGeneration T1 passes after 250ms");});
 for(const auto family:{YmccFamily::MsiClaw,YmccFamily::AsusRogAllyX,YmccFamily::Unknown})run("retained-wake-"+std::to_string(static_cast<int>(family)),[=]{g_machineIdentity.family=family;readRoute=family==YmccFamily::MsiClaw?"msi-dinput":"xinput";aoac=family==YmccFamily::Unknown;ack(100);retainedWake();check(g_wakeKeptTargetGeneration==2&&keptLogs==1,"actual retained branch records current wake");check(g_inputHostBoundPowerGeneration==1&&g_inputHostEpoch==7,"kept branch preserves original protocol/target identity");ack(1000);ack(1300);check(g_inputHostRecoveryEpisodeAttempts==0,"no artificial recovery attempts introduced");check(g_inputHostHealthySinceTick==1000,"current-generation ACKs rebuild ordinary health");check(t1()=="allowed","T1 admits proved kept target after fresh 250ms");check(inputHostProtocolPowerGenerationLocked()==1,"health never rewrites wire generation");});
 run("second-retained-wake-current-proof",[]{retainedWake();ack(1000);ack(1300);retainedWake();ack(2000);ack(2300);check(g_wakeKeptTargetGeneration==3,"actual kept branch advances proof on second wake");check(g_inputHostHealthySinceTick==2000,"second wake gets its own health window");check(t1()=="allowed","second kept wake is admissible");check(inputHostProtocolPowerGenerationLocked()==1,"second wake preserves original wire binding");});
 for(const auto kind:{"missing-kept","stale-kept","future-bound","zero-bound","no-target","not-ready","g_powerGeneration-revoked","transport-fault","exit","physical-window","intent-reset","external-priority","target-disabled","host-not-neutral"})run(kind,[=]{retainedWake();const std::string k=kind;if(k=="missing-kept")g_wakeKeptTargetGeneration=0;if(k=="stale-kept")g_wakeKeptTargetGeneration=1;if(k=="future-bound")g_inputHostBoundPowerGeneration=3;if(k=="zero-bound")g_inputHostBoundPowerGeneration=0;if(k=="no-target")g_inputHostProcess=false;if(k=="not-ready")g_powerLifecycle=PowerLifecycle::Resuming;if(k=="g_powerGeneration-revoked")g_inputHostPowerAdmissionRevoked=true;if(k=="transport-fault")g_inputHostTransportFault=true;if(k=="exit")exitFence=true;if(k=="physical-window")releaseWindow=true;if(k=="intent-reset")g_inputHostRecoveryIntentReset=true;if(k=="external-priority")g_externalPriorityVirtualSuspended=true;if(k=="target-disabled")g_realStickTest.virtualTargetEnabled=false;if(k=="host-not-neutral")g_inputHostNeutralized=false;ack(1000);ack(1300);check(g_inputHostHealthySinceTick==0,"unknown/revoked binding cannot form health");check(g_inputHostRecoveryEpisodeAttempts==0,"invalid contexts do not manufacture budget activity");});
 run("old-read-receipt-after-kept-wake",[]{const auto source=read(100);const auto request=inputHostRecoveryHealthContextLocked(false);retainedWake();read(1000);now=1100;inputHostRecoveryHealthAcceptedLocked(request,source,true);check(g_inputHostHealthySinceTick==0,"pre-wake read/ACK cannot start current health");});
 run("fresh-static-zero-frame-recovery",[]{retainedWake();ack(1000);ack(1300);check(g_inputCaptureGamepadSnapshot.pad.wButtons==0,"physical input stays all-zero");check(g_inputHostRecoveryHealth.through()==1300,"fresh zero source covers current endpoint");});
 run("pre-ready-read-never-healthy",[]{retainedWake();g_powerLifecycle=PowerLifecycle::Resuming;ack(1000);check(g_inputHostHealthySinceTick==0,"writer retention cannot waive Ready health gate");g_powerLifecycle=PowerLifecycle::Ready;ack(1200);check(g_inputHostRecoveryHealth.since()==1200,"Ready starts a new eligible window");});
 run("target-epoch-change-during-ack",[]{retainedWake();const auto source=read(1000);const auto request=inputHostRecoveryHealthContextLocked(false);++g_inputHostEpoch;inputHostRecoveryHealthAcceptedLocked(request,source,true);check(g_inputHostHealthySinceTick==0,"old target ACK cannot seed new epoch");});
 for(const auto kind:{"stale-read","unstable-read","topology-mismatch","identity-mismatch","source-absent","ineligible-neutral"})run(kind,[=]{retainedWake();auto source=read(1000);const std::string k=kind;if(k=="stale-read")now=2501;if(k=="unstable-read")source.stable=false;if(k=="topology-mismatch")source.topology=99;if(k=="identity-mismatch")source.identity=99;if(k=="source-absent")source.live=false;if(k=="ineligible-neutral")source=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,false);const auto request=inputHostRecoveryHealthContextLocked(false);inputHostRecoveryHealthAcceptedLocked(request,source,k!="ineligible-neutral");check(g_inputHostHealthySinceTick==0,"kept proof never bypasses real source eligibility/freshness");});
 run("new-keep-proof-does-not-bless-old-read",[]{const auto source=read(100);retainedWake();const auto request=inputHostRecoveryHealthContextLocked(false);now=1000;inputHostRecoveryHealthAcceptedLocked(request,source,true);check(g_inputHostHealthySinceTick==0,"new request and kept proof cannot restamp a pre-wake read");});
 run("request-power-retires-during-ack",[]{retainedWake();const auto source=read(1000);const auto request=inputHostRecoveryHealthContextLocked(false);++g_powerGeneration;g_wakeKeptTargetGeneration=g_powerGeneration.load();now=1100;inputHostRecoveryHealthAcceptedLocked(request,source,true);check(g_inputHostHealthySinceTick==0,"late ACK cannot cross another proved wake");});
 run("duplicate-read-does-not-extend-health",[]{retainedWake();const auto source=read(1000);const auto request=inputHostRecoveryHealthContextLocked(false);inputHostRecoveryHealthAcceptedLocked(request,source,true);const auto before=g_inputHostRecoveryHealth.through();now=1300;inputHostRecoveryHealthAcceptedLocked(request,source,true);check(g_inputHostRecoveryHealth.through()==before,"duplicate ACK cannot manufacture later read coverage");});
 run("non-retained-desktop-is-not-kept",[]{g_machineIdentity.family=YmccFamily::Unknown;aoac=false;++g_powerGeneration;keptWakeBranch();check(g_wakeKeptTargetGeneration==0&&keptLogs==0,"ordinary desktop never registers kept proof");ack(1000);check(g_inputHostHealthySinceTick==0,"unproved old desktop binding remains rejected");});
 std::cout<<json({{"cases",cases},{"assertions",assertions},{"failures",failures},{"trace",trace},{"actualAttemptsSeeded",false},{"deviceOrPipeIo",false}}).dump(2)<<std::endl;return failures.empty()?0:1;
}
