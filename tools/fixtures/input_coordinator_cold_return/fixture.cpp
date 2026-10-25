#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <string>
#include <vector>
#include <functional>
#include "virtual_target_return_record.h"
namespace ymcc::targetReturn::pnpAbsence {struct Result;}
namespace ymcc::targetReturn::physicalPath {struct Result;}
namespace ymcc::targetReturn::process {struct Probe;}
static std::string coldBinding();
static std::wstring coldWriterPath();
static ymcc::targetReturn::pnpAbsence::Result coldAbsence(const std::vector<std::string>&);
static bool coldHidden(std::vector<std::string>&);
static ymcc::targetReturn::physicalPath::Result coldProof(const std::string&,const std::string&);
static ymcc::targetReturn::process::Probe coldProbe(const ymcc::targetReturn::ProcessIdentity&);
static bool coldCli(const std::wstring&,std::string*,DWORD*);
// Reuse the existing offline ends and real coordinator TU; never call its old suites.
#define wmain originalCoordinatorFixtureMain
#include "base.fixture.cpp"
#undef wmain
namespace cold131 {
namespace duty=ymcc::targetReturn;
namespace runtime=ymcc::inputHost::targetReturnRuntime;
namespace continuation=ymcc::inputHost::switchExecutionReturnRuntime;
static const duty::ProcessIdentity oldParent{710131,"132000000000000131"},oldHost{710132,"132000000000000132"};
static const std::string path="HID\\VID_1234&PID_5678\\COLD131",root="USB\\VID_1234&PID_5678\\COLD131";
static duty::process::State nativeState=duty::process::State::Dead,hostState=duty::process::State::Dead;
static duty::pnpAbsence::State census=duty::pnpAbsence::State::Absent,pathCensus=duty::pnpAbsence::State::Absent;
static duty::physicalPath::State proofState=duty::physicalPath::State::Proven;
static duty::physicalPath::Reason proofReason=duty::physicalPath::Reason::RootMatched;
static CONFIGRET proofReturn=CR_SUCCESS;
static bool hiddenReadable=true,hiddenPresent=true;
static unsigned absenceCalls=0,pathCensusCalls=0,hiddenCalls=0,unhideCalls=0,proofCalls=0;
static std::function<void()> observationAction;
static json ports=json::array();
static void port(const char* name){ports.push_back({{"port",name},{"clock",startProtectionClock},{"deadline",g_inputHostSwitchRecoveryStartDeadline},{"request",g_inputHostSwitchRecoveryStartRequestId},{"power",currentPowerGeneration()}});}
static unsigned creates(){return static_cast<unsigned>(std::count(g_startEntrySeam.trace.begin(),g_startEntrySeam.trace.end(),"create-process"));}
static unsigned finals(std::uint64_t ticket){unsigned n=0;for(const auto& e:fixtureEvents)if(e.value("event","")=="input-host-switch-verification"&&e["detail"].value("stage","")=="final"&&e["detail"].value("requestId",0ULL)==ticket)++n;return n;}
static void reset(const std::string& name,bool off=false,unsigned protocol=1,bool pathDebt=true){
 if(g_inputHostProcess){cleanup();if(g_inputHostProcess){CloseHandle(g_inputHostProcess);g_inputHostProcess=nullptr;}}
 seed();cases.push_back({{"name","COLD131/"+name}});fixtureFullPipe=false;
 g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="ROG Ally X";
 g_msiDinput.enabled=false;g_msiDinputSourceValid=false;g_msiSteamDeckReleaseDebt.clear();g_psEndpointOwnership.clear();
 g_inputReady=true;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostPowerAdmissionRevoked=false;g_inputCaptureStop=false;g_exitRequested=false;
 g_externalPriorityVirtualSuspended=false;g_shortcutRecordingActive=false;g_inputHostEpoch=0;g_inputHostRunId.clear();g_inputHostHostInstanceId.clear();
 g_physicalReleaseWindowNeutral=false;g_physicalReleaseWindowStage=0;g_physicalReleaseWindowTransition=0;
 {std::lock_guard<std::mutex> lock(g_msiNativeForegroundMx);g_msiNativeForegroundLease.active=false;g_msiNativeForegroundLease.restorePending=false;g_msiNativeForegroundLease.cycleProcess=nullptr;}
 {std::lock_guard<std::mutex> lock(g_inputHostLifecycleWorkMx);g_inputHostLifecycleWorkQueue.clear();g_inputHostLifecycleWorkRequested=false;}
 g_realStickTest.enabled=true;g_realStickTest.virtualTargetEnabled=!off;g_realStickTest.virtualTargetPersona=off?"disabled":"steamdeck";g_inputShortcutRuntime.clear();
 fixtureXinputSourceLive=true;g_xinputAdmittedMask=1;g_xinputPhysicalSourceMask=1;g_xinputConnectedMask=1;fixtureFrameFirstAck=true;
 nativeState=hostState=duty::process::State::Dead;census=pathCensus=duty::pnpAbsence::State::Absent;
 proofState=duty::physicalPath::State::Proven;proofReason=duty::physicalPath::Reason::RootMatched;proofReturn=CR_SUCCESS;
 hiddenReadable=true;hiddenPresent=pathDebt;absenceCalls=pathCensusCalls=hiddenCalls=unhideCalls=proofCalls=0;observationAction={};ports=json::array();
 duty::Origin owner;owner.transactionId="cold131-old";owner.parent=oldParent;owner.host=oldHost;owner.hostInstanceId="cold131-old-host";owner.runId="cold131-old-run";owner.epoch=131;owner.targetId=kInputHostTargetId;owner.persona="steamdeck";owner.physicalRoot=pathDebt?root:"";owner.cliWriterProtocol=protocol;
 auto session=duty::Session::open(fixtureTargetReturnRecordPath(),coldBinding());if(!session.ready()||!session.reserveTarget(session.record()->revision,owner,{true,true})||(pathDebt&&!session.reservePath(session.record()->revision,owner,path,{true,true,true})))throw std::runtime_error("actual private cold history seed failed");
 check(!runtime::state.session,"history seeded on disk before actual runtime open");
}
static bool hasOld(){const auto r=duty::load(fixtureTargetReturnRecordPath(),coldBinding());return r.record&&std::any_of(r.record->obligations.begin(),r.record->obligations.end(),[](const auto& g){return g.origin.transactionId=="cold131-old";});}
static void advance(std::uint64_t ms){startProtectionClock+=ms;fixturePowerClockOffset+=ms;}
static void maintenance(){std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchExecutionReturnMaintenanceLocked();}
static void inspect(){std::lock_guard<InputHostMutex> lock(g_inputHostMx);inputHostSwitchVerificationMaintenanceLocked();}
static void pump(){fullPipeDrainQueuedLifecycle();}
static void resume(){advance(1000);maintenance();pump();}
static void retainFacts(){cases.back()["ports"]=ports;cases.back()["nativeEvents"]=fixtureEvents;cases.back()["state"]=state();cases.back()["oldDutyRetained"]=hasOld();}
static void sample(){auto v=fullPipeReadNow(1);{std::lock_guard<std::mutex> lock(g_inputCaptureGamepadSnapshotMx);const auto old=inputHostRecoveryHealthSource(&g_inputCaptureGamepadSnapshot,true);v.readContinuityGeneration=ymcc::input::recoveryHealth::nextContinuity(old,inputHostRecoveryHealthSource(&v,true),GetTickCount64());g_inputCaptureGamepadSnapshot=v;}fullPipeSubmitSource(v,true,false);}
static void run(){
 reset("ON-owned-hidden-return-before-new-Start");check(inputHostStart(),"real cold ON settles exact history before actual remaining Start");check(unhideCalls==1&&!hasOld()&&creates()==1,"one configuration-only unhide and one new Start after committed settlement");check(g_inputHostPrepared&&g_inputHostNeutralized,"real Start publishes prepared neutralized target through existing offline process/pipe ends");retainFacts();
 reset("ON-deferred-resume-original-budget-and-sole-final");hostState=duty::process::State::Unknown;const auto at=startProtectionClock;check(!inputHostStart()&&hasOld()&&creates()==0,"unknown old Host blocks target construction without consuming historical responsibility");const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;check(continuation::state.cold&&continuation::state.pending,"actual continuation owns the deferred cold player intent");hostState=duty::process::State::Dead;resume();check(!hasOld()&&creates()==1&&g_inputHostPrepared,"actual same-ticket queued cold continuation resumes one normal Start");check(g_virtualStartProtection.view(startProtectionClock).remainingMs==30000-(startProtectionClock-at)&&g_virtualStartProtection.view(startProtectionClock).fallbackAttempts==0,"cold return retains original thirty-second budget with zero fallback");for(int n=0;n<kInputHostFreshNeutralFramesRequired+2;++n)sample();const auto ready=g_virtualStartProtection.view(startProtectionClock);check(ready.phase=="verifying"&&ready.candidateOutcome=="success","actual accepted current source supplies candidate after cold Start");advance(ready.verificationRemainingMs);sample();inspect();inspect();check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&finals(ticket)==1,"actual inspection publishes exactly one final for original cold ON request");retainFacts();
 reset("OFF-startup-pre-ready-queue",true);g_inputReady=false;inputHostTargetReturnQueueColdStartup();check(g_inputHostLifecycleWorkRequested&&hasOld()&&unhideCalls==0,"pre-ready bootstrap only loads history and queues actual startup-owned-return");pump();const auto offTicket=g_virtualStartProtection.view(startProtectionClock).requestId;check(continuation::state.pending&&continuation::state.cold&&creates()==0&&hasOld()&&unhideCalls==0,"actual OFF worker pauses cold execution until current environment permits it");g_inputReady=true;resume();check(!hasOld()&&creates()==0&&!continuation::state.pending,"actual OFF continuation settles exact history without creating a new target");auto offView=g_virtualStartProtection.view(startProtectionClock);check(offView.phase=="verifying"&&offView.candidateOutcome=="success","OFF cleanup only creates a candidate for common inspection");advance(offView.verificationRemainingMs);inspect();inspect();check(g_virtualStartProtection.view(startProtectionClock).phase=="success"&&finals(offTicket)==1,"OFF completes through one real common final");retainFacts();
 for(const std::string pause:{"focus","input-ready","power","recording","native-lease"}){
  reset("current-pause-"+pause);auto set=[&](bool on){if(pause=="focus")g_externalPriorityVirtualSuspended=on;else if(pause=="input-ready")g_inputReady=!on;else if(pause=="power")g_powerLifecycle=on?PowerLifecycle::Suspending:PowerLifecycle::Ready;else if(pause=="recording")g_shortcutRecordingActive=on;else g_msiNativeForegroundLease.active=on;};hostState=duty::process::State::Unknown;check(!inputHostStart()&&creates()==0&&hasOld(),"actual unproven old writer establishes original cold continuation before a current pause");const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;hostState=duty::process::State::Dead;set(true);const auto passes=continuation::state.coldPasses;advance(3000);maintenance();pump();check(unhideCalls==0&&creates()==0&&continuation::state.coldPasses==passes,"known current pause does not consume cold observation quota or write configuration");set(false);maintenance();pump();check(creates()==1&&!hasOld()&&g_virtualStartProtection.view(startProtectionClock).requestId==ticket&&g_virtualStartProtection.view(startProtectionClock).remainingMs==27000,"resumed actual current environment uses only original remaining Start time");retainFacts();
 }
 reset("paused-at-original-thirty-second-cap");g_shortcutRecordingActive=true;check(!inputHostStart(),"cold execution waits during actual recording pause");const auto expiredTicket=g_virtualStartProtection.view(startProtectionClock).requestId;advance(30000);maintenance();g_shortcutRecordingActive=false;maintenance();pump();inspect();inspect();check(creates()==0&&unhideCalls==0&&hasOld()&&!continuation::state.pending&&g_virtualStartProtection.view(startProtectionClock).remainingMs==0,"pause at original deadline cannot renew lifetime or discharge old duty");check(finals(expiredTicket)==1,"expired original cold request has one common failure final");retainFacts();
 reset("cancel-queued-cold-return");hostState=duty::process::State::Unknown;check(!inputHostStart(),"unknown old writer defers cancel scenario");hostState=duty::process::State::Dead;advance(1000);maintenance();check(continuation::state.queued,"actual maintenance enqueues same cold request");check(g_virtualStartProtection.cancel(startProtectionClock),"caller cancellation retires the actual pending R3 intent");pump();maintenance();check(creates()==0&&unhideCalls==0&&hasOld()&&!continuation::state.pending&&g_virtualStartProtection.view(startProtectionClock).phase=="cancelled","late queued cold return cannot write or Start after cancellation");retainFacts();
 reset("legacy-protocol-blocks-new-Start",false,0);check(!inputHostStart()&&creates()==0&&unhideCalls==0&&hasOld(),"imported legacy-zero provenance blocks new Start without cleanup authority");resume();resume();check(creates()==0&&hasOld()&&g_inputHostFaultHoldsStart,"bounded actual observations preserve legacy duty and hold a new Start");retainFacts();
 for(const std::string change:{"expired","new-ticket","power","cancel"}){
  reset("OS-observation-retirement-"+change);census=duty::pnpAbsence::State::Present;check(!inputHostStart(),"first complete-present observation defers cold intent");resume();check(continuation::state.coldPasses==2,"actual second deferred observation reaches preterminal cold pass count");const auto oldTicket=g_virtualStartProtection.view(startProtectionClock).requestId;std::uint64_t newer=0;
  observationAction=[&]{if(change=="power")++g_powerGeneration;else if(change=="cancel")g_virtualStartProtection.cancel(startProtectionClock);else {advance(30000);if(change=="new-ticket")newer=g_virtualStartProtection.admit("steamdeck",startProtectionClock).requestId;}};
  resume();check(creates()==0&&unhideCalls==0&&hasOld()&&!continuation::state.pending&&!g_inputHostFaultHoldsStart,"retired OS observation cannot set failure hold or Start for old or newer intent");
  if(change=="new-ticket")check(newer&&newer!=oldTicket&&g_virtualStartProtection.view(startProtectionClock).requestId==newer&&g_virtualStartProtection.view(startProtectionClock).phase=="pending","old return preserves new ticket and its pending result ownership");retainFacts();
 }
 for(const std::string outcome:{"absent","present","unknown","other-ancestry-error"}){
  reset("missing-old-devnode-"+outcome);proofState=duty::physicalPath::State::Unknown;proofReason=outcome=="other-ancestry-error"?duty::physicalPath::Reason::ParentQueryFailed:duty::physicalPath::Reason::TargetLocateFailed;proofReturn=CR_NO_SUCH_DEVNODE;pathCensus=outcome=="present"?duty::pnpAbsence::State::Present:outcome=="unknown"?duty::pnpAbsence::State::Unknown:duty::pnpAbsence::State::Absent;
  const bool started=inputHostStart();if(outcome=="absent")check(started&&unhideCalls==1&&pathCensusCalls>0&&!hasOld(),"only exact missing-devnode plus complete exact-path/family absence permits configuration-only unhide");else check(!started&&unhideCalls==0&&hasOld()&&(outcome!="other-ancestry-error"||pathCensusCalls==0),"present unknown or other ancestry errors preserve exact old key without unhide");retainFacts();
 }
}
}
static std::string coldBinding(){return "windows-machine-guid:private-fixture-cold131";}
static std::wstring coldWriterPath(){return (fspath::path(fixtureTargetReturnRecordPath()).parent_path()/L"cli-writer.json").wstring();}
static ymcc::targetReturn::process::Probe coldProbe(const ymcc::targetReturn::ProcessIdentity& identity){using namespace cold131;port("PID-birth-model");duty::process::State value;if(duty::detail::sameProcess(identity,oldParent))value=nativeState;else if(duty::detail::sameProcess(identity,oldHost))value=hostState;else {++forbiddenIo;throw std::logic_error("unregistered-cold-writer-identity");}return {value,value==duty::process::State::Dead?duty::process::Reason::MatchingExited:duty::process::Reason::OpenFailed,ERROR_SUCCESS};}
static ymcc::targetReturn::pnpAbsence::Result coldAbsence(const std::vector<std::string>& known){using namespace cold131;++absenceCalls;port("complete-PnP-census-model");const bool exact=std::find(known.begin(),known.end(),path)!=known.end();if(exact)++pathCensusCalls;if(observationAction){auto action=std::move(observationAction);observationAction={};action();}duty::pnpAbsence::Result value;value.state=exact?pathCensus:census;value.reason=value.absent()?duty::pnpAbsence::Reason::CompleteAbsence:value.state==duty::pnpAbsence::State::Present?duty::pnpAbsence::Reason::ExactIdPresent:duty::pnpAbsence::Reason::EnumerationFailed;return value;}
static bool coldHidden(std::vector<std::string>& hidden){using namespace cold131;++hiddenCalls;port("complete-hidden-list-model");if(!hiddenReadable)return false;hidden=hiddenPresent?std::vector<std::string>{path}:std::vector<std::string>{};return true;}
static ymcc::targetReturn::physicalPath::Result coldProof(const std::string& r,const std::string& target){using namespace cold131;++proofCalls;port("exact-ancestry-model");if(r!=root||target!=path){++forbiddenIo;throw std::logic_error("foreign-cold-physical-path");}return {proofState,proofReason,1,proofReturn};}
static bool coldCli(const std::wstring& arguments,std::string* output,DWORD* code){using namespace cold131;if(fixtureHealthInventoryRead&&arguments==L"--dev-all"){fixtureHealthInventoryRead=false;if(output)*output="offline-readonly-health-observation";if(code)*code=0;return true;}if(arguments!=L"--dev-unhide \""+U2W(path)+L"\""){++forbiddenIo;throw std::logic_error("unadmitted-cold-driver-command");}port("exact-configuration-unhide-model");++unhideCalls;hiddenPresent=false;if(code)*code=0;return true;}
int wmain(int argc,wchar_t**argv){try{if(argc!=3)return 2;g_yemanccDataDir=argv[1];g_appDataDir=argv[1];fspath::create_directories(g_yemanccDataDir);cold131::run();const bool pass=failures.empty()&&forbiddenIo==0;json result={{"status",pass?"PASS":"FAIL"},{"caseCount",cases.size()},{"checks",checks},{"cases",cases},{"assertionTrace",assertionTrace},{"failures",failures},{"forbiddenIo",forbiddenIo},{"actualFullTranslationUnit",true},{"actualColdEnvironmentSessionAlgorithmContinuation",true},{"osDriverLeaves","explicit private file binding; registered PID/birth and complete census/hidden/ancestry/configuration models"},{"normalProductHostCliDevicesSteamRun",false},{"oldAllSuiteRun",false}};std::ofstream(fspath::path(argv[2]))<<result.dump(2)<<"\n";std::cout<<json{{"status",result["status"]},{"caseCount",cases.size()},{"checks",checks},{"failures",failures}}.dump()<<"\n";return pass?0:1;}catch(const std::exception& error){json result={{"status","INCOMPLETE"},{"exception",error.what()},{"checks",checks},{"failures",failures},{"events",fixtureEvents},{"ports",cold131::ports},{"state",state()}};if(argc==3)std::ofstream(fspath::path(argv[2]))<<result.dump(2)<<"\n";std::cerr<<error.what()<<"\n";return 3;}}