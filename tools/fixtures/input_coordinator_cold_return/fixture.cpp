#ifndef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#endif
// Explicit private cases layered over the original cold fixture. No copied cold
// algorithm; only existing IO leaves and observations are controlled below.
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <future>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <functional>
#include <string>
#include <vector>
#include "cold-repair-config.h"
static void coldRepairObserveGeneric(const char*);
static unsigned long coldRepairHelperExit();
static std::function<bool(std::vector<std::string>&)> coldRepairVisibility;
static std::function<void()> coldRepairCliHook;

#if COLD_REPAIR_EXPECT_EXCLUSIVE != 1
#error "Canonical cold fixture requires current imported exclusive cold-owner expectation"
#endif
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
static std::string path="HID\\VID_1234&PID_5678\\COLD131",root="USB\\VID_1234&PID_5678\\COLD131";
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
static bool coldHidden(std::vector<std::string>& hidden){using namespace cold131;++hiddenCalls;port("complete-hidden-list-model");if(!hiddenReadable)return false;if(coldRepairVisibility)return coldRepairVisibility(hidden);hidden=hiddenPresent?std::vector<std::string>{path}:std::vector<std::string>{};return true;}
static ymcc::targetReturn::physicalPath::Result coldProof(const std::string& r,const std::string& target){using namespace cold131;++proofCalls;port("exact-ancestry-model");if(r!=root||target!=path){++forbiddenIo;throw std::logic_error("foreign-cold-physical-path");}return {proofState,proofReason,1,proofReturn};}
static bool coldCli(const std::wstring& arguments,std::string* output,DWORD* code){using namespace cold131;if(fixtureHealthInventoryRead&&arguments==L"--dev-all"){fixtureHealthInventoryRead=false;if(output)*output="offline-readonly-health-observation";if(code)*code=0;return true;}if(arguments!=L"--dev-unhide \""+U2W(path)+L"\""){++forbiddenIo;throw std::logic_error("unadmitted-cold-driver-command");}if(coldRepairCliHook)coldRepairCliHook();port("exact-configuration-unhide-model");++unhideCalls;hiddenPresent=false;if(code)*code=0;return true;}
int originalColdFixtureEntry(int argc,wchar_t**argv){try{if(argc!=3)return 2;g_yemanccDataDir=argv[1];g_appDataDir=argv[1];fspath::create_directories(g_yemanccDataDir);cold131::run();const bool pass=failures.empty()&&forbiddenIo==0;json result={{"status",pass?"PASS":"FAIL"},{"caseCount",cases.size()},{"checks",checks},{"cases",cases},{"assertionTrace",assertionTrace},{"failures",failures},{"forbiddenIo",forbiddenIo},{"actualFullTranslationUnit",true},{"actualColdEnvironmentSessionAlgorithmContinuation",true},{"osDriverLeaves","explicit private file binding; registered PID/birth and complete census/hidden/ancestry/configuration models"},{"normalProductHostCliDevicesSteamRun",false},{"oldAllSuiteRun",false}};std::ofstream(fspath::path(argv[2]))<<result.dump(2)<<"\n";std::cout<<json{{"status",result["status"]},{"caseCount",cases.size()},{"checks",checks},{"failures",failures}}.dump()<<"\n";return pass?0:1;}catch(const std::exception& error){json result={{"status","INCOMPLETE"},{"exception",error.what()},{"checks",checks},{"failures",failures},{"events",fixtureEvents},{"ports",cold131::ports},{"state",state()}};if(argc==3)std::ofstream(fspath::path(argv[2]))<<result.dump(2)<<"\n";std::cerr<<error.what()<<"\n";return 3;}}

static void coldRepairWriteResult(const wchar_t*,const json&);
namespace coldRepair {
static std::string selectedCase;
static std::atomic<unsigned> work{0},maint{0},polls{0};
static std::atomic<int> terminal{-1};
static unsigned launches=0;static DWORD controlledExit=13;static bool exactInventory=true;
static std::mutex observedMx;static std::condition_variable observed;
static void need(bool value,const char* message){check(value,message);if(!value)throw std::runtime_error(message);}
template<class R>static bool rejected(const R& r){if constexpr(requires {r.preMutationRejected;})return r.preMutationRejected;else return false;}
template<class R>static unsigned resumes(const R& r){if constexpr(requires {r.preMutationResumes;})return r.preMutationResumes;else return 0;}
static void begin(const std::string& name,bool dispatch=true,const std::string& selectedRoot=fixtureGpdPostHideRoot()){
 coldRepairVisibility={};cold131::path=cold131::root=selectedRoot;cold131::reset(name,true);
 g_machineIdentity.resolved=true;g_machineIdentity.family=YmccFamily::Gpd;g_machineIdentity.manufacturer="GPD";g_machineIdentity.product="G1619-05";
 {std::lock_guard lease(g_msiNativeForegroundMx);g_msiNativeForegroundLease=MsiNativeForegroundLease{};}
 g_coldReturnCycleOwned=false;g_postHideCycleOwned=false;g_msiNativeForegroundCyclePending=false;g_msiNativeForegroundPowerHold=false;
 g_inputHostVirtualIntentWriters=0;g_msiNativeForegroundVirtualYieldPending=false;
 work=maint=polls=0;launches=0;terminal=-1;controlledExit=13;exactInventory=true;
 cold131::pathCensus=cold131::duty::pnpAbsence::State::Present;
 g_msiForegroundTestIo.locations=[](const std::string& r){return r==cold131::root?std::vector<std::wstring>{fixtureGpdPostHideLocation()}:std::vector<std::wstring>{};};
 g_msiNativeForegroundTestIo={};auto& io=g_msiNativeForegroundTestIo;
 io.ui=[](){return true;};io.queue=[](){};
 io.xusbInventory=[](const HidHidePhysicalIdentity& source){MsiNativeForegroundXusbInventory v;v.complete=true;ymcc::msi::NativeForegroundXusbInterface i;i.activeInterface=i.ancestorMatched=true;i.instanceId=exactInventory?cold131::root:std::string("USB\\VID_045E&PID_028E\\OTHER-CURRENT-ROOT");i.ancestorRoot=source.baseContainerDeviceInstanceId;v.interfaces={i};return v;};
 io.postHideRootStarted=[](const std::string& r){return r==cold131::root;};
 io.xusbRefresh=[](const HidHidePhysicalIdentity& p,const std::wstring& where){need(p.deviceInstanceId==cold131::root&&where==fixtureGpdPostHideLocation(),"exact cold root and original location");++launches;return 1;};
 io.poll=[](){++polls;return terminal.load();};
 const auto loaded=cold131::duty::load(fixtureTargetReturnRecordPath(),coldBinding());bool reserved=false;
 if(loaded.record)for(const auto& o:loaded.record->obligations)for(const auto& p:o.paths)reserved|=p.instanceId==cold131::root&&p.stage==cold131::duty::DispatchStage::Reserved;
 need(reserved,"actual Session reservation is Reserved before observed hidden return");
 if(!dispatch)return;
 inputHostTargetReturnQueueColdStartup();cold131::pump();
 need(g_coldReturnCycleOwned&&g_msiNativeForegroundLease.active&&g_msiNativeForegroundLease.coldReturnCycle&&g_msiNativeForegroundCyclePending,"real ColdEnvironment/ColdPass/continuation establishes pending cold owner");
 need(g_msiNativeForegroundLease.coldReturnReceipt.observedRootHidden&&launches==1,"original unhide observed exact Reserved root hidden and dispatched one controlled helper");
 need(cold131::continuation::state.cold&&cold131::continuation::state.pending&&cold131::creates()==0,"original OFF continuation is pending without new Host Start");
}
static void finishSame(unsigned expectedLaunches,std::uint64_t ticket,std::uint64_t deadline){
 terminal=0;cold131::resume();
 need(launches==expectedLaunches,"no extra helper dispatch beyond scenario allowance");
 need(inputHostConfiguredPersona()=="disabled"&&!g_realStickTest.virtualTargetEnabled&&cold131::creates()==0,"final OFF persona has no virtual target or new Host");
 const auto v=g_virtualStartProtection.view(startProtectionClock);
 need(v.requestId==ticket&&startProtectionClock<deadline&&v.remainingMs==deadline-startProtectionClock,"original request and absolute remaining budget retained after temporary execution Scope returned");
 need(!cold131::hasOld()&&!cold131::continuation::state.pending,"actual cold Session and continuation completed");
 need(v.phase=="verifying"&&v.candidateOutcome=="success","actual common OFF verification candidate");
 cold131::advance(v.verificationRemainingMs);cold131::inspect();cold131::inspect();
 need(cold131::finals(ticket)==1&&g_virtualStartProtection.view(startProtectionClock).phase=="success","one original common OFF final");cold131::retainFacts();
}
static void locks(){for(const bool host:{true,false})for(const bool maintenance:{false,true}){
 begin(std::string("lock-route/")+(host?"host/":"native/")+(maintenance?"maintenance":"work"));
 const auto ticket=g_msiNativeForegroundLease.coldReturnTicket,deadline=g_msiNativeForegroundLease.coldReturnDeadline;
 const auto before=polls.load(),beforeWork=work.load(),beforeMaint=maint.load();std::promise<void> ready,release,done;auto acquired=ready.get_future(),freed=release.get_future(),finished=done.get_future();
 std::exception_ptr failure;
 std::thread holder([&]{if(host){std::lock_guard<InputHostMutex> lock(g_inputHostMx);ready.set_value();freed.wait();}else{std::lock_guard<std::mutex> lock(g_msiNativeForegroundMx);ready.set_value();freed.wait();}});
 acquired.wait();std::thread caller([&]{try{if(maintenance)msiNativeForegroundMaintenance();else msiNativeForegroundHandleWork();}catch(...){failure=std::current_exception();}done.set_value();});
 bool decisive=false;
 if(COLD_REPAIR_EXPECT_EXCLUSIVE)decisive=finished.wait_for(std::chrono::seconds(5))==std::future_status::ready;
 else {std::unique_lock lock(observedMx);decisive=observed.wait_for(lock,std::chrono::seconds(5),[&]{return work.load()!=beforeWork||maint.load()!=beforeMaint;});}
 release.set_value();holder.join();caller.join();
 const auto afterWork=work.load(),afterMaint=maint.load(),afterPolls=polls.load();
 cases.back()["contention"]={{"held",host?"Host":"native"},{"entry",maintenance?"Maintenance":"HandleWork"},{"decisive",decisive},{"before",{{"genericWork",beforeWork},{"genericMaintenance",beforeMaint},{"polls",before}}},{"after",{{"genericWork",afterWork},{"genericMaintenance",afterMaint},{"polls",afterPolls}}},{"genericWork",afterWork-beforeWork},{"genericMaintenance",afterMaint-beforeMaint},{"polls",afterPolls-before},{"scope","real mutexes/actual functions; deltas start after ColdStartup pump; synthetic helper IO only"}};
 if(failure)std::rethrow_exception(failure);
 need(decisive,"actual entry reached deterministic contention observation before lock release");
 if(COLD_REPAIR_EXPECT_EXCLUSIVE){need(afterWork==beforeWork&&afterMaint==beforeMaint,"owned cold route stays exclusive while either real mutex is held");need(afterPolls==before,"no generic or cold poll while lock contention persists");}
 else need(afterWork!=beforeWork||afterMaint!=beforeMaint,"baseline falls through into actual generic foreground entry");
 need(!g_msiNativeForegroundLease.coldReturnAborted&&launches==1&&g_msiNativeForegroundCyclePending,"lock contention alone does not replace pending helper");
 finishSame(1,ticket,deadline);
}}
// Semantic baseline, independent of receipt member availability. Preserve actual
// old record and observed declared-success facts BEFORE asserting the defect.
static void baselineRepro(){
 begin("baseline-repro/old3-aborted-absent-same-period");
 const auto ticket=g_msiNativeForegroundLease.coldReturnTicket,deadline=g_msiNativeForegroundLease.coldReturnDeadline;
 g_physicalReleaseWindowStage=1;(void)msiColdReturnMaintainOwner("private-baseline-abort");
 controlledExit=3;terminal=2;(void)msiColdReturnMaintainOwner("private-baseline-old3-terminal");g_physicalReleaseWindowStage=0;
 cases.back()["typedReceiptObservation"]={{"rejected",rejected(g_msiNativeForegroundLease.coldReturnReceipt)},{"resumes",resumes(g_msiNativeForegroundLease.coldReturnReceipt)}};
 cold131::pathCensus=cold131::duty::pnpAbsence::State::Absent;terminal=-1;cold131::resume();
 const auto absentView=g_virtualStartProtection.view(startProtectionClock);
 cases.back()["baselineAbsentRaw"]={{"oldDutyRetained",cold131::hasOld()},{"record",cold131::duty::load(fixtureTargetReturnRecordPath(),coldBinding()).record.has_value()},{"declaredSuccess",absentView.candidateOutcome=="success"||absentView.phase=="success"},{"continuationPending",cold131::continuation::state.pending},{"ticket",ticket},{"deadline",deadline},{"clock",startProtectionClock}};
 const bool retained=cold131::hasOld(),declaredSuccess=absentView.candidateOutcome=="success"||absentView.phase=="success";
 const auto absentLaunches=launches;
 cold131::pathCensus=cold131::duty::pnpAbsence::State::Present;cold131::resume();
 const auto presentView=g_virtualStartProtection.view(startProtectionClock);
 const auto& receipt=g_msiNativeForegroundLease.coldReturnReceipt;
 cases.back()["baselineResumeRaw"]={{"launches",launches},{"oldDutyRetained",cold131::hasOld()},{"sameTicket",presentView.requestId==ticket},{"beforeDeadline",startProtectionClock<deadline},{"declaredSuccess",presentView.candidateOutcome=="success"||presentView.phase=="success"},{"spent",receipt.spent},{"accepted",receipt.accepted},{"preMutationRejected",rejected(receipt)},{"preMutationResumes",resumes(receipt)},{"expectedLaunches",1},{"terminalCode",3}};
 cold131::retainFacts();
 check(retained&&!declaredSuccess&&absentLaunches==1,"old3 + transient absent must retain original duty without declared success or another helper");
 check(launches==1&&cold131::hasOld()&&!rejected(receipt)&&resumes(receipt)==0&&receipt.spent&&!receipt.accepted,"root present after untyped old3 remains nonretryable and retains unresolved original duty");
 check(presentView.requestId==ticket&&startProtectionClock<deadline&&presentView.candidateOutcome!="success"&&presentView.phase!="success","old3 root return preserves original period without a false success");
}

static auto actualColdPass(std::uint64_t ticket,std::uint64_t power){
 std::lock_guard<InputHostMutex> host(g_inputHostMx);
 return inputHostTargetReturnColdPassLocked(ticket,power,"disabled");
}
static void rejectedTerminal13(){
 g_inputHostVirtualIntentWriters=1;(void)msiColdReturnMaintainOwner("private-rejection-pending");
 controlledExit=13;terminal=2;(void)msiColdReturnMaintainOwner("private-rejection-terminal");g_inputHostVirtualIntentWriters=0;
}
static void failedDutySuccessor(){for(const std::string change:{"new-OFF-ticket","power"}){
 begin("failed-duty-successor/"+change);
 const auto ticket=g_msiNativeForegroundLease.coldReturnTicket,power=currentPowerGeneration(),deadline=g_msiNativeForegroundLease.coldReturnDeadline;
 rejectedTerminal13();need(cold131::hasOld()&&g_msiNativeForegroundLease.coldReturnReceipt.spent,"actual failed first helper retains original exact-origin obligation");
 terminal=-1;(void)actualColdPass(ticket,power);
 need(launches==2&&resumes(g_msiNativeForegroundLease.coldReturnReceipt)==1,"actual ColdEnvironment admits only the one original-period preIO resume");
 rejectedTerminal13();need(cold131::hasOld()&&resumes(g_msiNativeForegroundLease.coldReturnReceipt)==1,"exhausted typed13 retains exact original origin before context succession");
 std::uint64_t successor=ticket,newPower=power;
 if(change=="new-OFF-ticket"){
  cold131::advance(deadline>startProtectionClock?deadline-startProtectionClock+1:1);
  successor=g_virtualStartProtection.admit("disabled",startProtectionClock).requestId;
  need(successor&&successor!=ticket,"new OFF is a real distinct admitted request");
 }else{++g_powerGeneration;newPower=currentPowerGeneration();}
 cold131::pathCensus=cold131::duty::pnpAbsence::State::Absent;terminal=-1;
 const auto result=actualColdPass(successor,newPower);
 cases.back()["failedDutySuccessorRaw"]={{"change",change},{"oldTicket",ticket},{"currentTicket",successor},{"oldPower",power},{"currentPower",newPower},{"state",static_cast<int>(result.state)},{"oldDutyRetained",cold131::hasOld()},{"launches",launches},{"spent",g_msiNativeForegroundLease.coldReturnReceipt.spent},{"accepted",g_msiNativeForegroundLease.coldReturnReceipt.accepted}};
 cold131::retainFacts();
 check(cold131::hasOld(),"failed spent exact-origin duty survives ticket/power change and transient root absence");
 check(result.state!=cold131::duty::coldExecution::State::Clean&&launches==2,"new context cannot borrow absent root as restoration or old typed resume authority");
}}
static void pendingZeroAbsence(){
 begin("pending-zero-absence/accepted-before-absence");
 const auto ticket=g_msiNativeForegroundLease.coldReturnTicket,power=currentPowerGeneration();
 terminal=0;
 // The real ColdEnvironment polls the pending operation before the next census.
 // Only that existing census response becomes absent; no direct receipt mutation.
 cold131::observationAction=[](){cold131::pathCensus=cold131::duty::pnpAbsence::State::Absent;};
 const auto result=actualColdPass(ticket,power);
 cases.back()["pendingZeroRaw"]={{"state",static_cast<int>(result.state)},{"accepted",g_msiNativeForegroundLease.coldReturnReceipt.accepted},{"oldDutyRetained",cold131::hasOld()},{"launches",launches},{"polls",polls.load()}};
 cold131::retainFacts();
 check(g_msiNativeForegroundLease.coldReturnReceipt.accepted,"actual pollOwnColdRefresh terminal0 commits accepted before transient census absence");
 check(result.state==cold131::duty::coldExecution::State::Clean&&!cold131::hasOld()&&launches==1,"accepted helper completes original exact duty without duplicate cycle or false failure");
}
static void typed(){
 for(const std::string mode:{"writer","borrow","old3","error6","error8","unknown","exact-inventory","generation-only","new-ticket","power","on","deadline","exhaust"}){
  begin("typed-preio/"+mode);const auto ticket=g_msiNativeForegroundLease.coldReturnTicket,deadline=g_msiNativeForegroundLease.coldReturnDeadline;
  const auto power=currentPowerGeneration();const auto identity=g_xinputSlotIdentityEpoch.load();
  if(mode=="writer")g_inputHostVirtualIntentWriters=1;else g_physicalReleaseWindowStage=1;
  (void)msiColdReturnMaintainOwner("private-controlled-pause");
  need(g_msiNativeForegroundLease.coldReturnAborted&&g_msiNativeForegroundCyclePending,"actual owner cleanup revokes pending controlled helper before terminal");
  controlledExit=mode=="old3"?3:mode=="error6"?6:mode=="error8"?8:mode=="unknown"?259:13;
  terminal=2;(void)msiColdReturnMaintainOwner("private-controlled-terminal");
  g_inputHostVirtualIntentWriters=0;g_physicalReleaseWindowStage=0;
  need(g_msiNativeForegroundLease.coldReturnReceipt.spent,"spent survives actual lease cleanup");
  const bool eligible=controlledExit==13;
  need(rejected(g_msiNativeForegroundLease.coldReturnReceipt)==eligible,"only typed terminal13 supplies pre-mutation receipt; old3 and failures do not");
  if(mode=="exact-inventory")exactInventory=false;
  if(mode=="generation-only")++g_xinputSlotIdentityEpoch;
  if(mode=="power")++g_powerGeneration;
  if(mode=="on"){g_realStickTest.virtualTargetEnabled=true;g_realStickTest.virtualTargetPersona="steamdeck";}
  if(mode=="deadline")cold131::advance(deadline-startProtectionClock);
  if(mode=="new-ticket"){cold131::advance(30000);g_virtualStartProtection.admit("disabled",startProtectionClock);}
  const bool deny=!eligible||mode=="exact-inventory"||mode=="power"||mode=="on"||mode=="deadline"||mode=="new-ticket";
  if(deny){cold131::resume();need(launches==1,"retired identity/request/power/persona or untyped failure cannot resume");cold131::retainFacts();continue;}
  if(mode=="writer")++g_inputHostVirtualIntentRevision; // same admitted request, completed seqlock writer
  cold131::pathCensus=cold131::duty::pnpAbsence::State::Absent;terminal=-1;cold131::resume();
  need(cold131::hasOld()&&cold131::continuation::state.pending&&launches==1,"short root absence cannot settle failed/awaiting-resume cold responsibility");
  cold131::pathCensus=cold131::duty::pnpAbsence::State::Present;cold131::resume();
  need(launches==2&&g_msiNativeForegroundCyclePending&&resumes(g_msiNativeForegroundLease.coldReturnReceipt)==1,"same current exact root admits at most one preIO resume");
  need(currentPowerGeneration()==power&&g_msiNativeForegroundLease.coldReturnTicket==ticket&&g_msiNativeForegroundLease.coldReturnDeadline==deadline,"resume preserves original identity power request and deadline");
  if(mode=="exhaust"){controlledExit=13;terminal=2;g_physicalReleaseWindowStage=1;(void)msiColdReturnMaintainOwner("private-second13");g_physicalReleaseWindowStage=0;terminal=-1;cold131::resume();need(launches==2,"second typed13 cannot dispatch a third helper");cold131::retainFacts();continue;}
  finishSame(2,ticket,deadline);
 }
}
}
namespace coldRepair {
using Env=ymcc::inputHost::targetReturnRuntime::ColdEnvironment;
static std::uint64_t direct(const std::string& name){
 begin(name,false);std::lock_guard<InputHostMutex> host(g_inputHostMx);
 need(cold131::runtime::open(),"actual Session opens private historical record");
 const auto a=g_virtualStartProtection.admit("disabled",startProtectionClock);
 need(a.accepted&&a.requestId,"actual original OFF admission");return a.requestId;
}
static bool durable(){const auto loaded=cold131::duty::load(fixtureTargetReturnRecordPath(),coldBinding());
 if(!loaded.record)return false;for(const auto& g:loaded.record->obligations)for(const auto& p:g.paths)
  if(p.instanceId==cold131::root&&p.stage==cold131::duty::DispatchStage::MayHaveIssued)return true;return false;}
static HANDLE blockRecord(){return CreateFileW(fixtureTargetReturnRecordPath().c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);}
static std::wstring sharedRecord(){wchar_t b[32768]{};const auto n=GetEnvironmentVariableW(L"YMCC_COLD_SHARED_RECORD",b,32768);need(n&&n<32768,"explicit private shared record");return std::wstring(b,n);}
static void bindShared(bool writer){
 cold131::runtime::state={};const auto target=sharedRecord();
 if(writer){fspath::create_directories(fspath::path(target).parent_path());need(fspath::copy_file(fixtureTargetReturnRecordPath(),target),"copy initial actual reservation to explicit crash record");}
 fixtureTargetReturnPath=target;
 std::lock_guard<InputHostMutex> host(g_inputHostMx);need(cold131::runtime::open(),"fresh process loads same on-disk record");
}
static void cutWriter(const std::string& cut,const wchar_t* result){
 (void)direct("durable-write/"+cut);bindShared(true);const auto ticket=g_virtualStartProtection.view(startProtectionClock).requestId;
 const auto saveCut=[&]{need(durable()&&cold131::hasOld(),"root duty persisted before crash boundary");
  coldRepairWriteResult(result,{{"status","PASS"},{"cut",cut},{"durable",durable()},{"hidden",cold131::hiddenPresent},{"launches",launches},{"cases",cases},{"checks",checks},{"failures",failures},{"abruptProcessExit",true}});ExitProcess(0);};
 {std::lock_guard<InputHostMutex> host(g_inputHostMx);Env env{ticket,currentPowerGeneration(),"disabled"};
  if(cut=="marker")coldRepairCliHook=saveCut;
  need(env.unhide(cold131::root),"actual unhide after durable stage commit");coldRepairCliHook={};}
 if(cut=="unhide")saveCut();
 need(cut=="helper","known abrupt boundary");
 (void)actualColdPass(ticket,currentPowerGeneration());need(launches==1,"one pending controlled helper at helper cut");
 terminal=0;{std::lock_guard lease(g_msiNativeForegroundMx);need(msiNativeForegroundPollCycleLocked()==0,"actual terminal poll before any path consume");}
 need(!g_msiNativeForegroundCyclePending&&!g_msiNativeForegroundLease.cycleProcess&&!g_msiNativeForegroundLease.nativeRefreshWriter,"writer leaf retired before helper cut");saveCut();
}
static void cutReader(const std::string& cut){
 const auto ticket=direct("durable-read/"+cut);bindShared(false);need(durable(),"fresh OS process sees prior committed root stage with no old memory");
 need(!g_msiNativeForegroundLease.coldReturnReceipt.spent&&!g_msiNativeForegroundLease.coldReturnReceipt.observedRootHidden,"fresh process has no borrowed old receipt or retry authority");
 cold131::hiddenPresent=cut=="marker";
 for(const auto census:{cold131::duty::pnpAbsence::State::Unknown,cold131::duty::pnpAbsence::State::Absent}){
  cold131::pathCensus=census;const auto r=actualColdPass(ticket,currentPowerGeneration());
  need(!r.clean()&&cold131::hasOld()&&durable()&&launches==0,"unknown or transient absent cannot erase persisted root duty or issue cycle");
 }
 cold131::pathCensus=cold131::duty::pnpAbsence::State::Present;terminal=-1;
 (void)actualColdPass(ticket,currentPowerGeneration());need(launches==1&&g_msiNativeForegroundCyclePending,"new process current exact root gets one fresh authorized helper");
 need(resumes(g_msiNativeForegroundLease.coldReturnReceipt)==0,"new process does not borrow old preIO resume");
 terminal=0;const auto r=actualColdPass(ticket,currentPowerGeneration());need(r.clean()&&!cold131::hasOld()&&launches==1,"fresh exact helper success settles original durable duty");cold131::retainFacts();
}
static void supplemental(){
 {const auto ticket=direct("supplement/real-file-commit-failure");std::lock_guard<InputHostMutex> host(g_inputHostMx);Env env{ticket,currentPowerGeneration(),"disabled"};
  const auto origin=cold131::runtime::state.session->record()->obligations.front().origin;const auto lock=blockRecord();need(lock!=INVALID_HANDLE_VALUE,"private record locked against replace");
  const bool unhidden=env.unhide(cold131::root),consumed=env.consumePath(origin,cold131::root);need(CloseHandle(lock)!=FALSE,"release private record lock");
  need(!unhidden&&!consumed&&env.rootProofCommitFailed&&cold131::hiddenPresent&&cold131::unhideCalls==0&&launches==0&&!durable(),"real commit failure blocks unhide cycle and consume despite unhide return being optional");
  ++g_inputHostVirtualIntentRevision;Env retry{ticket,currentPowerGeneration(),"disabled"};need(retry.unhide(cold131::root)&&durable()&&!cold131::hiddenPresent,"current same OFF can persist previous exact observation and unhide after file returns");
  need(!g_msiNativeForegroundLease.active&&!g_msiNativeForegroundLease.coldReturnReceipt.spent,"observation intent rebind does not dispatch or refund an attempt");cold131::retainFacts();}
 {const auto ticket=direct("supplement/root-first-multiple-paths");
  {std::lock_guard<InputHostMutex> host(g_inputHostMx);auto& s=*cold131::runtime::state.session;const auto origin=s.record()->obligations.front().origin;
   need(s.reservePath(s.record()->revision,origin,"HID\\VID_045E&PID_028E&IG_00\\COLD-CHILD",{true,true,true}),"actual same-origin child follows root in record");}
  (void)actualColdPass(ticket,currentPowerGeneration());need(launches==1,"multi-path starts one exact root helper");terminal=0;
  const auto r=actualColdPass(ticket,currentPowerGeneration());need(r.clean()&&!cold131::hasOld()&&launches==1,"root first and child path settle after async0 without recreating root marker or second cycle");cold131::retainFacts();}
 {const auto ticket=direct("supplement/stale-origin-no-promotion");std::lock_guard<InputHostMutex> host(g_inputHostMx);Env env{ticket,currentPowerGeneration(),"disabled"};
  const auto lock=blockRecord();need(lock!=INVALID_HANDLE_VALUE,"lock prevents old marker commit");need(!env.unhide(cold131::root),"old observation captured with failed commit");need(CloseHandle(lock)!=FALSE,"unlock private record");
  auto& s=*cold131::runtime::state.session;const auto old=s.record()->obligations.front().origin;
  need(s.consumePath(s.record()->revision,old,cold131::root)&&s.consumeOrigin(s.record()->revision,old),"controlled history change retires old exact origin");
  auto next=old;next.transactionId+="-replacement";next.runId+="-replacement";
  need(s.reserveTarget(s.record()->revision,next,{true,true})&&s.reservePath(s.record()->revision,next,cold131::root,{true,true,true}),"new same-root origin remains only Reserved");
  Env fresh{ticket,currentPowerGeneration(),"disabled"};need(!fresh.prepareColdRoot(next)&&!durable()&&launches==0,"old exact-origin observation cannot promote a different same-root origin");cold131::retainFacts();}
 for(const bool rog:{true,false}){const auto ticket=direct(std::string("supplement/other-family/")+(rog?"rog":"known-unsupported"));
  std::lock_guard<InputHostMutex> host(g_inputHostMx);Env env{ticket,currentPowerGeneration(),"disabled"};need(env.unhide(cold131::root)&&durable(),"seed observed duty through actual GPD consumer");
  if(rog){g_machineIdentity.family=YmccFamily::AsusRogAllyX;g_machineIdentity.manufacturer="ASUSTEK COMPUTER INC.";g_machineIdentity.product="ROG Ally X";}
  else{g_machineIdentity.family=YmccFamily::Unknown;g_machineIdentity.manufacturer="OTHER";g_machineIdentity.product="OTHER";}
  const auto origin=cold131::runtime::state.session->record()->obligations.front().origin;
  need(env.prepareColdRoot(origin)&&launches==0,"known non GPD retains original pass-through without new GPD cycle");cold131::retainFacts();
 }
 coldRepairCliHook={};coldRepairVisibility={};
}
}

static void coldRepairObserveGeneric(const char* tag){if(std::string_view(tag)=="work")++coldRepair::work;else ++coldRepair::maint;{std::lock_guard lock(coldRepair::observedMx);}coldRepair::observed.notify_all();}
static unsigned long coldRepairHelperExit(){return coldRepair::controlledExit;}
static void coldRepairWriteResult(const wchar_t* file,const json& value){
 HANDLE h=CreateFileW(file,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(h==INVALID_HANDLE_VALUE)throw std::runtime_error("result exists or exclusive creation failed");
 json receipt=value;
 receipt["selection"]=coldRepair::selectedCase;receipt["caseCount"]=cases.size();receipt["assertionTrace"]=assertionTrace;
 receipt["controlledHelperIo"]=true;receipt["helperTerminalQueryFactsSource"]="controlled-test-only-helper-terminal-leaf";
 receipt["fullStartHostQualified"]=false;receipt["realRun15MappingQualified"]=false;receipt["realHelperJobRetirementQualified"]=false;receipt["actualSdkCyclePortExecuted"]=false;receipt["externalInputDeliveryProven"]=false;
 const auto bytes=receipt.dump(2)+"\n";DWORD written=0;const bool ok=WriteFile(h,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr)!=FALSE&&written==bytes.size();const bool closed=CloseHandle(h)!=FALSE;
 if(!ok||!closed)throw std::runtime_error("exclusive result write incomplete");
}

int wmain(int argc,wchar_t** argv){
 if(argc==3)return originalColdFixtureEntry(argc,argv); // original default group unchanged
 if(argc!=4)return 2;
 const std::wstring selected=argv[3];
 if(selected==L"original-cold-environment")return originalColdFixtureEntry(3,argv);
 const bool known=selected==L"lock-route"||selected==L"typed-preio"||selected==L"baseline-repro"||selected==L"failed-duty-successor"||selected==L"pending-zero-absence"||selected==L"supplement"||
  selected==L"durable-write-marker"||selected==L"durable-read-marker"||selected==L"durable-write-unhide"||selected==L"durable-read-unhide"||selected==L"durable-write-helper"||selected==L"durable-read-helper";
 if(!known){std::cerr<<"unknown explicit cold case\n";return 2;}
 try{coldRepair::selectedCase=W2U(selected);g_yemanccDataDir=argv[1];g_appDataDir=argv[1];fspath::create_directories(g_yemanccDataDir);
  if(selected==L"lock-route")coldRepair::locks();else if(selected==L"typed-preio")coldRepair::typed();else if(selected==L"baseline-repro")coldRepair::baselineRepro();else if(selected==L"failed-duty-successor")coldRepair::failedDutySuccessor();else if(selected==L"pending-zero-absence")coldRepair::pendingZeroAbsence();else if(selected==L"supplement")coldRepair::supplemental();else if(selected.rfind(L"durable-write-",0)==0)coldRepair::cutWriter(W2U(selected.substr(14)),argv[2]);else if(selected.rfind(L"durable-read-",0)==0)coldRepair::cutReader(W2U(selected.substr(13)));else throw std::runtime_error("unknown explicit private case");
  const bool ok=failures.empty()&&forbiddenIo==0;json result={{"status",ok?"PASS":"FAIL"},{"cases",cases},{"checks",checks},{"failures",failures},{"forbiddenIo",forbiddenIo},{"scope","actual cold coordinator/record/continuation and real mutexes; controlled synthetic helper endpoint"},{"fullStartHostQualified",false},{"realRun15MappingQualified",false},{"realHelperJobRetirementQualified",false},{"externalInputDeliveryProven",false}};coldRepairWriteResult(argv[2],result);return ok?0:1;
 }catch(const std::exception& e){coldRepairWriteResult(argv[2],json{{"status",failures.empty()?"INCOMPLETE":"FAIL"},{"exception",e.what()},{"cases",cases},{"failures",failures}});return 3;}
}
