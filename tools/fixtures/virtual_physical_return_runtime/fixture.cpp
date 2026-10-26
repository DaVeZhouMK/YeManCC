#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
#include "input_host_transaction_mutex.h"
#include "virtual_start_protection.h"
#include "ps_endpoint_ownership.h"
#include "virtual_target_return_record.h"
#include "virtual_target_return_physical_path.h"
#include "virtual_target_return_process.h"
#include "hidhide_cli_writer_store.h"
#include "hidhide_cli_job_guard.h"
using json=nlohmann::json;using USAGE=USHORT;namespace fspath=std::filesystem;
#include "types.inc"
#include "paths.inc"
using InputHostMutex=ymcc::inputHost::TransactionMutex<>;
static InputHostMutex g_inputHostMx;
static std::mutex g_hidHideMx;
static HANDLE g_hidHidePendingChildProcess=nullptr;
static HANDLE g_hidHidePendingReadPipe=nullptr;
static ymcc::hidHideCli::JobGuard g_hidHidePendingJob;
static std::unique_ptr<ymcc::hidhideCliWriter::Session> g_hidHidePendingWriter;
static std::string g_hidHidePendingAttempt;
static std::wstring fixtureCliWriterPath;
static const std::string fixtureCliAttemptId="fixture-cli-writer-124";
static ymcc::targetReturn::ProcessIdentity fixtureCliOldParent{700201,"132000000000000011"},fixtureCliOldChild{700202,"132000000000000012"};
static ymcc::targetReturn::process::State fixtureCliParentState=ymcc::targetReturn::process::State::Dead,fixtureCliChildState=ymcc::targetReturn::process::State::Dead;
static std::vector<ymcc::targetReturn::ProcessIdentity> fixtureCliProbes;
static unsigned fixtureCliJobProbes=0,fixtureForbiddenIo=0;
namespace ymcc::hidHideCliWriterRuntime {enum class JobState;}
static ymcc::hidHideCliWriterRuntime::JobState fixtureCliJobState=static_cast<ymcc::hidHideCliWriterRuntime::JobState>(0);
static ymcc::hidHideCliWriterRuntime::JobState fixtureCliProbeJob(const std::string& name);
static ymcc::targetReturn::process::Probe fixtureCliProbe(const ymcc::targetReturn::ProcessIdentity& identity) {
 fixtureCliProbes.push_back(identity);using namespace ymcc::targetReturn::process;
 State state=State::Unknown;
 if(ymcc::targetReturn::detail::sameProcess(identity,fixtureCliOldParent))state=fixtureCliParentState;
 else if(ymcc::targetReturn::detail::sameProcess(identity,fixtureCliOldChild))state=fixtureCliChildState;
 else {++fixtureForbiddenIo;throw std::logic_error("unexpected-foreign-CLI-process-model-identity");}
 return {state,state==State::Dead?Reason::MatchingExited:state==State::Alive?Reason::MatchingRunning:Reason::OpenFailed,static_cast<DWORD>(state==State::Unknown?ERROR_ACCESS_DENIED:ERROR_SUCCESS)};
}
static bool g_inputHostTransportFault=false;
static ymcc::targetReturn::process::State fixtureWriterState=ymcc::targetReturn::process::State::Dead;
static std::vector<ymcc::targetReturn::ProcessIdentity> fixtureWriterProbes;
static std::atomic<std::uint64_t> g_hidResolveCacheGeneration{1},g_xinputTopologyEpoch{1},g_msiMainSourceRevision{1};
static std::uint64_t fixturePower=1,fixtureClock=GetTickCount64();
static std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0,g_inputHostSwitchRecoveryStartRequestId=0,g_inputHostSwitchRecoveryStartPower=0;
static ymcc::VirtualStartProtection g_virtualStartProtection;
static ymcc::psEndpointOwnership::State g_psEndpointOwnership;
static HANDLE g_inputHostProcess=reinterpret_cast<HANDLE>(std::uintptr_t{1});
static std::string g_inputHostHostInstanceId="owned-host-1",g_inputHostRunId="owned-run-1",fixturePersona="steamdeck";
static std::uint64_t g_inputHostEpoch=1;
static std::uint64_t g_inputHostStartProtectionBoundTicket=1; // Explicit fixture scalar, no native ticket authority.
static const char* kInputHostTargetId="fixture-target-111";
static std::uint32_t fixtureHostPid=700001,fixtureExpectedDebtHostPid=700001;
static bool fixtureCaptureFails=false,fence=false;
static std::uint64_t currentPowerGeneration(){return fixturePower;}
static std::uint64_t inputHostStartProtectionNow(){return fixtureClock;}
static bool inputHostExitFenceRaised(){return fence;}
static std::string inputHostBoundOrCurrentPersona(){return fixturePersona;}
static std::wstring fixtureCaseRoot,fixtureRecordPath;
static bool virtualIdentity=false,identityOk=true,snapshotOk=true,reservationOk=true;
static unsigned fixtureProofCalls=0;
static ymcc::targetReturn::physicalPath::State fixtureProofState=ymcc::targetReturn::physicalPath::State::Proven;
static std::function<void()> fixtureAfterProof,fixtureAfterCommit;
static std::vector<json> events,operations;
static bool fixtureCommitObserved=false;
static std::string fixtureMachineBinding(){
#ifdef YMCC_APPLY_INTEGRATION_ONLY
 return "windows-machine-guid:private-apply-integration"; // Explicit fake strong-binding leaf, never registry access.
#else
 return "fixture-machine";
#endif
}
static std::string fixtureNativeExtraDonorPath;
static unsigned fixtureNativeProofExpireAt=0,fixtureSnapshotFailAt=0,fixtureAcceptCalls=0;
static std::vector<std::string> fixtureNativeProofTargets;
struct CrashInterrupted final {};
static bool fixtureCrashAfterFirstHide=false;
static unsigned fixtureHandoffSnapshotCalls=0,fixtureHandoffSnapshotExpireAt=0;
static std::vector<std::string> fixtureAuthorityTrace;
// Explicit fake donor authority leaf; production GPD/ticket/helper guards are
// tested by the foreground consumer, never replaced by the reassert bool.
static bool fixtureNativeDonorProof=false,fixtureNativeDonorHeld=true;
static unsigned fixtureNativeProofCalls=0;
static std::string fixtureNativeDonorPath;
static HidHideVisibilitySnapshot fixtureNativeDonorBaseline;
static bool msiNativeForegroundReassertionBaseline(const std::string& target,HidHideVisibilitySnapshot& baseline){
 ++fixtureNativeProofCalls;fixtureNativeProofTargets.push_back(target);fixtureAuthorityTrace.push_back("path:"+target);
 if(fixtureNativeProofExpireAt==fixtureNativeProofCalls)fixtureNativeDonorProof=false;
 if(!fixtureNativeDonorProof||!fixtureNativeDonorHeld||(target!=fixtureNativeDonorPath&&target!=fixtureNativeExtraDonorPath))return false;
 baseline=fixtureNativeDonorBaseline;return true;
}

// Mode leaf only: a held donor still requires the separate exact Snapshot proof.
static bool msiNativeForegroundVirtualHandoffRequired(std::uint64_t){fixtureAuthorityTrace.push_back("held-mode");return fixtureNativeDonorHeld;}
// Explicit native whole-donor authority leaf, available to the real batch hook.
// The old Apply never calls the batch hook; this model does not add any record.
static bool msiNativeForegroundNativeHandoffSnapshot(std::vector<std::string>& exactOwned,HidHideVisibilitySnapshot& originalBaseline){
 ++fixtureHandoffSnapshotCalls;fixtureAuthorityTrace.push_back("whole-snapshot");exactOwned.clear();originalBaseline={};
 if(fixtureHandoffSnapshotExpireAt==fixtureHandoffSnapshotCalls)fixtureNativeDonorProof=false;
 if(!fixtureNativeDonorProof||!fixtureNativeDonorHeld||fixtureNativeDonorPath.empty())return false;
 exactOwned.push_back(fixtureNativeDonorPath);
 if(!fixtureNativeExtraDonorPath.empty())exactOwned.push_back(fixtureNativeExtraDonorPath);
 originalBaseline=fixtureNativeDonorBaseline;return true;
}
static HidHideVisibilityJournal g_hidHideVisibilityJournal;
static std::vector<HidHidePhysicalIdentity> fixtureSources;
static std::vector<std::string> fixtureTargets;
static std::set<std::string> world;
static bool pipeOk=true,cliOk=true,pipeApplies=true,cliApplies=true;
static unsigned snapshots=0,health=0,applyFallback=0;
static unsigned checks=0;static json failures=json::array(),cases=json::array();static std::string fixtureCurrentCase;
static void check(bool value,const char* assertion){++checks;if(!value)failures.push_back({{"case",fixtureCurrentCase},{"assertion",assertion}});}
static void appendNativeLifecycleLog(const char* event,json facts){
 events.push_back({{"event",event?event:""},{"facts",facts}});
 if(event&&std::strcmp(event,"input-host-target-return-record")==0&&(facts.value("phase",std::string{})=="physical-path-reserved"||facts.value("phase",std::string{})=="native-owned-set-reserved")){
  fixtureCommitObserved=true;auto action=std::move(fixtureAfterCommit);fixtureAfterCommit={};if(action)action();
 }
}
static bool hidHideIsVirtualIdentity(const std::string&,const std::string&){return virtualIdentity;}
static std::wstring fixturePath(){return fixtureRecordPath;}
static std::optional<ymcc::targetReturn::ProcessIdentity> fixtureCapture(HANDLE process){
 if(process==GetCurrentProcess())return ymcc::targetReturn::process::capture(process);
 if(fixtureCaptureFails||!process)return std::nullopt;
 // Controlled native Host owner token; not an OS child/SDK observation.
 return ymcc::targetReturn::ProcessIdentity{fixtureHostPid,"132000000000000001"};
}
static ymcc::targetReturn::physicalPath::Result fixtureProof(const std::string&,const std::string&){
 ++fixtureProofCalls;auto action=std::move(fixtureAfterProof);fixtureAfterProof={};if(action)action();
 return {fixtureProofState,fixtureProofState==ymcc::targetReturn::physicalPath::State::Proven?ymcc::targetReturn::physicalPath::Reason::RootMatched:ymcc::targetReturn::physicalPath::Reason::ParentQueryFailed,2,CR_SUCCESS};
}
static ymcc::targetReturn::process::Probe fixtureProbeOwner(const ymcc::targetReturn::ProcessIdentity& owner){
 fixtureWriterProbes.push_back(owner);
 using namespace ymcc::targetReturn::process;
 if(fixtureWriterState==State::Alive)return {State::Alive,Reason::MatchingRunning,ERROR_SUCCESS};
 if(fixtureWriterState==State::Dead)return {State::Dead,Reason::MatchingExited,ERROR_SUCCESS};
 return {State::Unknown,Reason::OpenFailed,ERROR_ACCESS_DENIED};
}
// Entire actual runtime header; its ENV/CM and process-observation leaves are rewritten by
// the runner. Before/After, Session, store, record transforms remain unchanged.
#include "writer-runtime.fixture.h"
static ymcc::hidHideCliWriterRuntime::JobState fixtureCliProbeJob(const std::string& name) {
 ++fixtureCliJobProbes;
 if(name!=std::string(ymcc::hidhideCliWriter::jobPrefix)+fixtureCliAttemptId){++fixtureForbiddenIo;throw std::logic_error("unexpected-foreign-CLI-job-model-name");}
 return fixtureCliJobState;
}
// Entire actual main reaper; exact pending root is an explicit Event model,
// while its writer Session/owner lease and all settlement/file operations are real.
#include "reaper.inc"
#include "runtime.fixture.h"
namespace duty=ymcc::targetReturn;namespace runtime=ymcc::inputHost::targetReturnRuntime;
static duty::LoadResult disk(){return duty::load(fixtureRecordPath,fixtureMachineBinding());}
static std::string bytes(){std::ifstream in{fspath::path(fixtureRecordPath),std::ios::binary};return{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};}
static std::string encoded(const duty::Record& record){std::string text;if(!duty::encode(record,text))throw std::runtime_error("fixture cannot encode actual record");return text;}
static std::size_t pathCount(const duty::LoadResult& loaded){std::size_t count=0;if(loaded.record)for(const auto& group:loaded.record->obligations)count+=group.paths.size();return count;}
static const duty::Obligation* pathOwner(const duty::LoadResult& loaded,const std::string& path){if(loaded.record)for(const auto& group:loaded.record->obligations)for(const auto& item:group.paths)if(duty::detail::fold(item.instanceId)==duty::detail::fold(path))return &group;return nullptr;}
static void driverObservation(const char* leaf,const std::string& path){
 const auto loaded=disk();const auto* group=pathOwner(loaded,path);
 const bool owned=group&&std::any_of(runtime::state.physicals.begin(),runtime::state.physicals.end(),[&](const auto& p){return duty::sameOrigin(p.origin,group->origin);});
 check(loaded.state==duty::LoadState::Valid&&group&&owned,"actual Refresh reserves exact physical path in durable record before any driver call");
 check(group&&group->origin.host.pid==fixtureExpectedDebtHostPid,"driver dispatch preserves expected original Host responsibility");
 check(hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,path),"actual caller keeps local path responsibility before driver dispatch");
 operations.push_back({{"leaf",leaf},{"path",path},{"durableBeforeDriver",group!=nullptr},{"ownedAtDispatch",owned},{"recordRevision",loaded.record?loaded.record->revision:0}});
}
enum class YmccInterfacePolicy {Other,MsiClawDinput};struct Machine {int family=0;};static Machine g_machineIdentity;
static YmccInterfacePolicy ymccFamilyInterfacePolicy(int family){return family==3?YmccInterfacePolicy::MsiClawDinput:YmccInterfacePolicy::Other;}
static void inputHostExitFenceLogRefusal(const char*,const char*){}
static void hidHideServiceHealth(const char*){++health;}
static bool hidHideGetPhysicalIdentities(std::vector<HidHidePhysicalIdentity>& out){if(!identityOk||fixtureSources.empty())return false;out=fixtureSources;return true;}
static bool hidHideCaptureVisibilitySnapshot(HidHideVisibilitySnapshot& out){++snapshots;if(!snapshotOk||(fixtureSnapshotFailAt&&snapshots>=fixtureSnapshotFailAt))return false;out={};out.valid=true;out.cloaking=true;
#ifdef YMCC_APPLY_INTEGRATION_ONLY
 out.applications={L"C:\\fixture-private\\YeManCC.exe"}; // Preexisting registration, no application write.
#endif
for(const auto& path:world)out.hidden.push_back(U2W(path));return true;}
#ifdef YMCC_APPLY_INTEGRATION_ONLY
static bool hidHideApplyPhysicalTransaction(const std::string&,bool);
static bool hidHideRefreshPhysicalHide(const std::string&,const char*);
#else
// Legacy Refresh boundary only; never compiled in the actual Apply consumer.
static bool hidHideApplyPhysicalTransaction(const std::string&,bool){++applyFallback;return false;}
#endif
static std::vector<std::string> hidHideVirtualPhysicalTargets(const HidHidePhysicalIdentity&,const HidHideVisibilitySnapshot&,std::size_t,const char*){return fixtureTargets;}
static bool msiCrashRecoveryBeforeHide(const HidHidePhysicalIdentity&,const std::vector<std::string>&,const HidHideVisibilitySnapshot&,const std::string&){return reservationOk;}
static bool inputHostHidHideApplyViaPipeLocked(const std::wstring& action,const std::wstring& path){if(action!=L"hide")throw std::logic_error("unadmitted driver action");const auto value=W2U(path);driverObservation("pipe",value);if(pipeApplies)world.insert(value);
 if(fixtureCrashAfterFirstHide&&operations.size()==1){
  fixtureCrashAfterFirstHide=false;
  events.push_back({{"event","CrashInterrupted-first-actual-hide-leaf"},{"path",value},{"recordBytesBeforeCrash",bytes()},{"worldBeforeCrash",world},{"authorityTargetsBeforeCrash",fixtureNativeProofTargets},{"wholeSnapshotCalls",fixtureHandoffSnapshotCalls}});
  throw CrashInterrupted{}; // Escapes actual Apply directly: no normal rollback.
 }
 return pipeOk;}
static bool hidHideRun(const std::wstring& args){const std::wstring prefix=L"--dev-hide \"";if(args.rfind(prefix,0)!=0||args.back()!=L'"')throw std::logic_error("unadmitted CLI action");const auto value=W2U(args.substr(prefix.size(),args.size()-prefix.size()-1));driverObservation("CLI",value);if(cliApplies)world.insert(value);return cliOk;}
static std::wstring hidHideCurrentModulePath(){
#ifdef YMCC_APPLY_INTEGRATION_ONLY
 return L"C:\\fixture-private\\YeManCC.exe";
#else
 return {};
#endif
}
static bool msiControllerTraceEnabled(){return false;}
static json msiControllerVisibilityApplications(const HidHideVisibilitySnapshot&,bool){return json::object();}
static void msiControllerTraceLog(const char*,json,bool){}
#ifdef YMCC_APPLY_INTEGRATION_ONLY
// Explicit Apply environment leaves. Unexpected mutation branches throw before IO.
// No native GPD state machine is claimed by these donor models.
static ULONGLONG g_hidHideLastIdentityBlockedLog=0;
namespace YmccFamily {static constexpr int LenovoLegionGo=2;}
static const char* ymccFamilyName(int){return "fixture-other";}
static bool msiClawKnownBoardAuthority(){return false;}
static void hidHideInvalidateRawInputIdentityCache(){++g_hidResolveCacheGeneration;}
static std::vector<std::wstring> hidHideExemptApplicationPaths(){return {hidHideCurrentModulePath()};}
static bool msiNativeForegroundAppendHandoffTargets(std::vector<std::string>&){return true;}
static bool inputHostLegionWirelessFromHid(){++fixtureForbiddenIo;throw std::logic_error("unexpected-wireless-leaf");}
static void hidHideCycleClearBusyIfStale(const char*){++fixtureForbiddenIo;throw std::logic_error("unexpected-cycle-busy-leaf");}
static bool hidHideCycleInstance(const std::wstring&,const char*){++fixtureForbiddenIo;throw std::logic_error("unexpected-device-cycle-leaf");}
static void inputCaptureLogVisibilitySnapshot(const char*,bool){}
static void msiForegroundIsolationJournalReady(){}
static bool msiNativeForegroundAcceptVirtualJournal(const HidHideVisibilitySnapshot& after){
 ++fixtureAcceptCalls;fixtureAuthorityTrace.push_back("accept");
 // Model only the final authority leaf. Production Apply must reach this leaf
 // after exact readback and actual reservation; assert the observed inputs.
 check(after.valid&&after.cloaking,"Apply supplies completed modeled readback to donor acceptance leaf");
 if(!fixtureNativeDonorProof)return g_hidHideVisibilityJournal.hiddenByUs.empty()&&fixtureNativeProofExpireAt==0;
 for(const auto& target:{fixtureNativeDonorPath,fixtureNativeExtraDonorPath}){
  if(target.empty())continue;
  const auto loaded=disk();const auto* owned=pathOwner(loaded,target);
  check(owned&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,target),"modeled transfer sees actual durable and journal responsibility for each donor path");
  if(!owned||!hidHideContainsPath(after.hidden,target))return false;
 }
 fixtureNativeDonorHeld=false;return true;
}
#include "apply-support.inc"
#include "apply.inc"
#include "virtual_target_return_cold_execution.h"
#endif
#include "refresh.inc"
static const std::string rootId="USB\\VID_1234&PID_5678\\PHYSICAL",pathA="HID\\VID_1234&PID_5678\\A",pathB="HID\\VID_1234&PID_5678\\B";
static unsigned caseSerial=0;
static void reset(const std::string& name){
 runtime::state=runtime::State{};fixtureCurrentCase=name;
 g_hidHidePendingWriter.reset();g_hidHidePendingJob.reset();g_hidHidePendingAttempt.clear();
 if(g_hidHidePendingChildProcess)CloseHandle(g_hidHidePendingChildProcess);
 if(g_hidHidePendingReadPipe)CloseHandle(g_hidHidePendingReadPipe);
 g_hidHidePendingChildProcess=nullptr;g_hidHidePendingReadPipe=nullptr;
 fixtureCliParentState=fixtureCliChildState=duty::process::State::Dead;fixtureCliJobState=ymcc::hidHideCliWriterRuntime::JobState::Absent;fixtureCliProbes.clear();fixtureCliJobProbes=0;fixtureForbiddenIo=0;
 const auto directory=fspath::path(fixtureCaseRoot)/(L"case-"+std::to_wstring(++caseSerial));fspath::create_directories(directory);fixtureRecordPath=(directory/L"record.json").wstring();fixtureCliWriterPath=(directory/L"cli-writer.json").wstring();
 g_hidHideVisibilityJournal={};g_hidHideVisibilityJournal.active=true;g_hidHideVisibilityJournal.transactionId="journal-1";g_hidHideVisibilityJournal.before.valid=true;
 fixtureNativeDonorProof=false;fixtureNativeDonorHeld=true;fixtureNativeProofCalls=0;fixtureNativeDonorPath=pathA;
 fixtureNativeExtraDonorPath.clear();fixtureNativeProofTargets.clear();fixtureNativeProofExpireAt=fixtureSnapshotFailAt=fixtureAcceptCalls=0;
 fixtureCrashAfterFirstHide=false;fixtureHandoffSnapshotCalls=fixtureHandoffSnapshotExpireAt=0;fixtureAuthorityTrace.clear();
 fixtureNativeDonorBaseline={};fixtureNativeDonorBaseline.valid=true;
#ifdef YMCC_APPLY_INTEGRATION_ONLY
 fixtureNativeDonorHeld=false; // No native Apply donor until seedNativeDonor explicitly supplies one.
#endif

 fixtureSources={HidHidePhysicalIdentity{}};fixtureSources[0].resolved=true;fixtureSources[0].deviceInstanceId=pathA;fixtureSources[0].baseContainerDeviceInstanceId=rootId;
 fixtureTargets={pathA};world={};events={};operations={};fixtureAfterProof={};fixtureAfterCommit={};fixtureCommitObserved=false;fixtureProofCalls=0;
 fixtureProofState=duty::physicalPath::State::Proven;virtualIdentity=false;identityOk=snapshotOk=reservationOk=true;pipeOk=cliOk=pipeApplies=cliApplies=true;fence=false;fixtureCaptureFails=false;
 snapshots=health=applyFallback=0;g_inputHostTransportFault=false;g_hidHidePendingChildProcess=nullptr;fixtureWriterState=duty::process::State::Dead;fixtureWriterProbes.clear();g_machineIdentity.family=1;fixtureClock=GetTickCount64();fixturePower=1;g_hidResolveCacheGeneration=1;g_xinputTopologyEpoch=1;g_msiMainSourceRevision=1;
 g_inputHostProcess=reinterpret_cast<HANDLE>(std::uintptr_t{1});fixtureHostPid=fixtureExpectedDebtHostPid=700001;g_inputHostHostInstanceId="owned-host-1";g_inputHostRunId="owned-run-1";g_inputHostEpoch=1;fixturePersona="steamdeck";
 g_virtualStartProtection.~VirtualStartProtection();new(&g_virtualStartProtection)ymcc::VirtualStartProtection();const auto ticket=g_virtualStartProtection.admit("steamdeck",fixtureClock);if(!ticket.accepted)throw std::runtime_error("real protection ticket not admitted");
 g_inputHostSwitchRecoveryStartRequestId=ticket.requestId;g_inputHostSwitchRecoveryStartPower=fixturePower;g_inputHostSwitchRecoveryStartDeadline=fixtureClock+30000;g_psEndpointOwnership.clear();
}
static bool refresh(){std::lock_guard<InputHostMutex> lock(g_inputHostMx);return hidHideRefreshPhysicalHide(g_hidHideVisibilityJournal.transactionId,"private-physical-runtime");}
static HidHideVisibilitySnapshot absent(){HidHideVisibilitySnapshot out;out.valid=true;return out;}
static bool settle(const HidHideVisibilitySnapshot& after,bool readback,const std::vector<std::wstring>& owned){std::lock_guard<InputHostMutex> lock(g_inputHostMx);return inputHostTargetReturnAfterPhysicalRestoreLocked(after,readback,owned);}
static void run(const char* name,const std::function<void()>& body){const auto start=failures.size();try{reset(name);body();}catch(const std::exception& e){failures.push_back({{"case",name},{"exception",e.what()}});}if(fixtureForbiddenIo)failures.push_back({{"case",name},{"assertion","unregistered CLI model identity is forbidden I/O"}});cases.push_back({{"name",name},{"pass",start==failures.size()},{"model","controlled discovery, CM proof and donor authority; actual selected consumer/runtime/Session/store"},{"operations",operations},{"events",events}});}
struct FileLock {HANDLE handle=INVALID_HANDLE_VALUE;explicit FileLock(const std::wstring& file){handle=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("could not acquire private actual no-share-delete handle");}~FileLock(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}FileLock(const FileLock&)=delete;FileLock&operator=(const FileLock&)=delete;};
// Real private event supplies WAIT_TIMEOUT/signaled only as an explicit CLI
// process-status model. No CLI process is launched or target handle queried.
struct PendingWriterModel {HANDLE handle=nullptr;PendingWriterModel(){handle=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!handle)throw std::runtime_error("private writer event model failed");g_hidHidePendingChildProcess=handle;}~PendingWriterModel(){if(g_hidHidePendingChildProcess==handle){g_hidHidePendingChildProcess=nullptr;if(handle)CloseHandle(handle);} /* else actual reaper already closed the model root */}void signal(){if(!SetEvent(handle))throw std::runtime_error("writer event signal failed");}};
static void seedEmpty(){duty::Record record;record.machineBinding=fixtureMachineBinding();check(duty::store(fixtureRecordPath,record).committed(),"private empty record seeded through actual store");}
// Actual BeforeHide caller boundary: retain responsibility even when dispatch
// is refused after commit; only true proceeds to the same hide leaf.
static bool reassertNativeOwned(bool nativeOwnedReassertion=true){
 std::lock_guard<InputHostMutex> lock(g_inputHostMx);
 HidHideVisibilitySnapshot current;if(!hidHideCaptureVisibilitySnapshot(current))return false;
 bool reserved=false;
 const bool dispatch=inputHostTargetReturnBeforeHideLocked(fixtureSources,current,g_hidHideVisibilityJournal.before,
  pathA,g_hidHideVisibilityJournal.transactionId,&reserved,nativeOwnedReassertion);
 if(reserved&&!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA))
  g_hidHideVisibilityJournal.hiddenByUs.push_back(U2W(pathA));
 if(!dispatch)return false;
 return inputHostHidHideApplyViaPipeLocked(L"hide",U2W(pathA));
}
static void seedNativeDonor(){
 fixtureNativeDonorProof=true;fixtureNativeDonorHeld=true;world.insert(pathA);
 // Caller original already includes hidden path; leaf alone supplies baseline.
 g_hidHideVisibilityJournal.before.hidden={U2W(pathA)};
}

#ifdef YMCC_APPLY_INTEGRATION_ONLY
static const std::string foreignPath="HID\\VID_7777&PID_7777\\FOREIGN";
static bool applyNative(){std::lock_guard<InputHostMutex> lock(g_inputHostMx);return hidHideApplyPhysicalTransaction("apply-journal-1",false);}
static void prepareApply(bool twoPaths=false){
 seedNativeDonor();g_hidHideVisibilityJournal={};
 if(twoPaths){fixtureNativeExtraDonorPath=pathB;world.insert(pathB);fixtureTargets={pathA,pathB};}
 world.insert(foreignPath);fixtureTargets.push_back(foreignPath);
}
static void observeDebt(const char* phase){const auto loaded=disk();events.push_back({{"event",phase},{"record",loaded.record?json::parse(encoded(*loaded.record)):json(nullptr)},{"journalOwned",[&](){json owned=json::array();for(const auto& path:g_hidHideVisibilityJournal.hiddenByUs)owned.push_back(W2U(path));return owned;}()},{"donorHeldModel",fixtureNativeDonorHeld},{"proofCalls",fixtureNativeProofCalls},{"proofTargets",fixtureNativeProofTargets},{"authorityTrace",fixtureAuthorityTrace},{"wholeSnapshotCalls",fixtureHandoffSnapshotCalls},{"acceptCalls",fixtureAcceptCalls}});}
static void retainExactDebt(const std::string& prior,std::size_t expectedPaths=1){
 check(operations.empty()&&pathCount(disk())==expectedPaths,"committed invalidated Apply emits zero hide and retains every exact durable batch path");
 const auto loaded=disk();const auto* group=pathOwner(loaded,pathA);
 const auto* other=expectedPaths==2?pathOwner(loaded,pathB):group;
 check(group&&other&&duty::sameOrigin(group->origin,other->origin)&&group->origin.physicalRoot==rootId&&group->origin.host.pid==fixtureExpectedDebtHostPid&&group->origin.runId==g_inputHostRunId,"retained complete batch pins one original root, Host, run and unchanged origin");
 check(hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA)&&(expectedPaths!=2||hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathB))&&g_hidHideVisibilityJournal.hiddenByUs.size()==expectedPaths&&fixtureNativeDonorHeld&&fixtureAcceptCalls==0,"Apply publishes EVERY committed batch member to journal on false, without modeled donor transfer");
 check(loaded.record&&loaded.record->revision==2&&(prior.empty()||bytes()!=prior)&&group&&std::all_of(group->paths.begin(),group->paths.end(),[](const auto& path){return path.stage==duty::DispatchStage::Reserved;}),"one real atomic Session revision retains all paths as Reserved, without fabricating Issued");
 observeDebt("retained-exact-post-commit-debt");
}
// Same Environment API as the original cold fixture. Only ports are modeled;
// run(), all record transforms, Session opens/consumes and file IO are original.
struct ApplyColdEnvironment {
 std::unique_ptr<duty::Session> session;
 duty::Record produced;
 unsigned unhideCalls=0,consumeCalls=0;
 json trace=json::array();
 explicit ApplyColdEnvironment(const duty::Record& original):produced(original){
  session.reset(new duty::Session(duty::Session::open(fixtureRecordPath,fixtureMachineBinding())));
  if(!session->ready()||encoded(*session->record())!=encoded(produced))throw std::runtime_error("exact Apply record must survive Session reopen");
 }
 bool current(){return true;} // Controlled fresh original recovery ticket.
 const duty::Record* record(){return session->record();}
 bool nativeDead(const duty::ProcessIdentity& p){const bool exact=std::any_of(produced.obligations.begin(),produced.obligations.end(),[&](const auto& g){return duty::detail::sameProcess(g.origin.parent,p);});check(exact,"cold native-death port observes exact Apply parent PID/birth");trace.push_back({{"port","nativeDead-model"},{"pid",p.pid},{"birth",p.creationTime}});return exact;}
 bool hostDead(const duty::ProcessIdentity& p){const bool exact=std::any_of(produced.obligations.begin(),produced.obligations.end(),[&](const auto& g){return duty::detail::sameProcess(g.origin.host,p);});check(exact,"cold Host-death port observes exact Apply Host PID/birth");trace.push_back({{"port","hostDead-model"},{"pid",p.pid},{"birth",p.creationTime}});return exact;}
 bool writersQuiet(){trace.push_back({{"port","writersQuiet-model"}});return true;}
 bool targetAbsent(const std::vector<std::string>& known){check(known.empty(),"Apply physical debt supplies no synthetic accepted target pin");trace.push_back({{"port","complete-target-absence-model"},{"knownIds",known}});return true;}
 bool visibility(std::vector<std::string>& hidden){hidden.assign(world.begin(),world.end());trace.push_back({{"port","visibility-model"},{"hidden",hidden}});return true;}
 bool pathProven(const std::string& root,const std::string& path){const auto loaded=disk();const auto* group=pathOwner(loaded,path);const bool exact=group&&group->origin.physicalRoot==root&&root==rootId;trace.push_back({{"port","ancestry-model"},{"root",root},{"path",path},{"exact",exact}});return exact;}
 bool unhide(const std::string& path){const auto loaded=disk();const auto* group=pathOwner(loaded,path);check(group&&path!=foreignPath,"cold unhide port receives only exact actual Apply debt");if(!group||path==foreignPath)throw std::logic_error("cold foreign mutation refused");++unhideCalls;world.erase(path);trace.push_back({{"port","unhide-model"},{"path",path},{"recordRevision",session->record()->revision}});return true;}
 bool consumePath(const duty::Origin& origin,const std::string& path){++consumeCalls;const auto revision=session->record()->revision;const bool ok=session->consumePath(revision,origin,path);trace.push_back({{"port","actual-Session-consumePath"},{"origin",origin.transactionId},{"path",path},{"beforeRevision",revision},{"committed",ok},{"afterRevision",session->record()->revision}});return ok;}
 bool consumeOrigin(const duty::Origin& origin){const auto revision=session->record()->revision;const bool ok=session->consumeOrigin(revision,origin);trace.push_back({{"port","actual-Session-consumeOrigin"},{"origin",origin.transactionId},{"committed",ok}});return ok;}
};
static void applyInterruptCase(){
 run("Apply/first-hide-interrupted-reopened-cold-restores-entire-native-donor",[]{
  prepareApply(true);
  check(fixtureNativeDonorPath==pathA&&fixtureNativeExtraDonorPath==pathB&&fixtureNativeDonorProof&&fixtureNativeDonorHeld,"interruption begins with exact native-owned A+B and explicit donor authority");
  check(world.count(pathA)&&world.count(pathB)&&world.count(foreignPath),"native A+B and excluded foreign are already hidden before actual Apply");
  fixtureCrashAfterFirstHide=true;bool interrupted=false;
  try{(void)applyNative();}catch(const CrashInterrupted&){interrupted=true;}
  check(interrupted&&operations.size()==1&&operations[0]["path"]==pathA,"dedicated crash escapes actual Apply at first real hide leaf before B hide or normal completion");
  check(fixtureAcceptCalls==0,"crashed actual Apply never reaches modeled donor accept or normal rollback");
  const auto loaded=disk();if(!loaded.record)throw std::runtime_error("crash input must be the actual Apply persisted record");
  const auto produced=*loaded.record;const auto persisted=bytes();
  observeDebt("actual-Apply-first-hide-interrupted-original-record");
  events.push_back({{"event","interruption-exact-boundary"},{"nativeOwn",{pathA,pathB}},{"foreignExcluded",foreignPath},{"durableA",pathOwner(loaded,pathA)!=nullptr},{"durableB",pathOwner(loaded,pathB)!=nullptr},{"durableForeign",pathOwner(loaded,foreignPath)!=nullptr},{"world",world},{"wholeSnapshotCalls",fixtureHandoffSnapshotCalls}});
  check(pathOwner(loaded,pathA)!=nullptr,"first actual hide has original exact A durable responsibility");
  check(pathOwner(loaded,pathB)!=nullptr,"entire native donor B must be durable before first actual hide can be interrupted");
  check(pathOwner(loaded,foreignPath)==nullptr,"foreign prehidden is never persisted as native-owned debt");
  // Model abrupt producer memory loss, never invoke Restore/After or seed a tail.
  runtime::state=runtime::State{};g_hidHideVisibilityJournal={};
  g_inputHostProcess=nullptr;g_inputHostHostInstanceId.clear();g_inputHostRunId.clear();
  fixtureNativeDonorProof=false;fixtureNativeDonorHeld=false;fixtureNativeDonorPath.clear();fixtureNativeExtraDonorPath.clear();fixtureNativeDonorBaseline={};
  fixtureSources.clear();fixtureTargets.clear();fixtureNativeProofTargets.clear();
  check(bytes()==persisted,"producer memory loss leaves actual Apply record byte-for-byte intact; no fixture tail repair");
  ApplyColdEnvironment env(produced);const auto result=duty::coldExecution::run(env);
  check(result.clean(),"actual coldExecution completes only the duties it found in reopened original record");
  check(!world.count(pathA),"cold return restores native-owned A after first-hide interruption");
  check(!world.count(pathB),"cold return must restore native-owned B even though B hide was never reached");
  check(world.count(foreignPath),"cold return preserves excluded foreign prehidden path");
  check(result.settledPaths==2&&result.unhideAttempts==2&&env.unhideCalls==2,"interrupted handoff cold return must consume and return both native-owned A+B");
  events.push_back({{"event","actual-coldExecution-after-first-hide-interruption"},{"cleanForRecordedDutiesOnly",result.clean()},{"reason",duty::coldExecution::reasonName(result.reason)},{"unhideAttempts",result.unhideAttempts},{"settledPaths",result.settledPaths},{"settledOrigins",result.settledOrigins},{"worldAfterCold",world},{"ownAReturned",!world.count(pathA)},{"ownBReturned",!world.count(pathB)},{"foreignPreserved",world.count(foreignPath)!=0},{"recordBeforeCrash",json::parse(encoded(produced))},{"recordAfterCold",json::parse(encoded(*env.record()))},{"controlledEnvironmentTrace",env.trace},{"recordTailSynthesizedByFixture",false}});
  env.session.reset();auto reopened=duty::Session::open(fixtureRecordPath,fixtureMachineBinding());
  check(reopened.ready()&&reopened.record()->obligations.empty(),"second actual Session reopen verifies recorded duties were consumed durably");
  check(!world.count(pathA)&&!world.count(pathB)&&world.count(foreignPath),"reopened record emptiness is insufficient: both native-owned A+B must be returned and foreign preserved");
 });
}
static void applyBatchAuthorityCases(){
 run("Apply/held-mode-without-Snapshot-proof-zero-hide",[]{
  prepareApply(true);fixtureNativeDonorProof=false;
  check(fixtureNativeDonorHeld&&!applyNative(),"held-only mode cannot substitute for independent whole-Snapshot native authority");
  check(fixtureHandoffSnapshotCalls==1&&fixtureNativeProofCalls==0&&operations.empty()&&pathCount(disk())==0&&g_hidHideVisibilityJournal.hiddenByUs.empty(),"failed whole-Snapshot proof emits zero hide and creates no durable or journal claim");
 });
 run("Apply/whole-Snapshot-retires-before-atomic-commit-zero-hide",[]{
  prepareApply(true);fixtureHandoffSnapshotExpireAt=2;
  check(!applyNative()&&fixtureNativeDonorHeld,"real batch core refuses complete donor proof that expires before atomic commit");
  check(fixtureHandoffSnapshotCalls==2&&fixtureNativeProofCalls==0&&operations.empty()&&pathCount(disk())==0&&g_hidHideVisibilityJournal.hiddenByUs.empty(),"precommit Snapshot retirement preserves native donor with zero hide and zero partial persisted batch");
 });
}
static void applyIntegrationCases(){
 run("Apply/native-prehidden-durable-same-hide-then-real-AfterRestore",[]{
  prepareApply(true);check(applyNative(),"actual Apply accepts two native-prehidden donor paths after real durable reservations");
  check(operations.size()==2&&pathCount(disk())==2&&g_hidHideVisibilityJournal.hiddenByUs.size()==2,"only two donor paths are rehidden and owned; foreign prehidden remains unclaimed");
  const std::vector<std::string> expectedAuthority={"held-mode","whole-snapshot","whole-snapshot","whole-snapshot","path:"+pathA,"path:"+pathA,"path:"+pathA,"path:"+pathB,"path:"+pathB,"path:"+pathB,"path:"+foreignPath,"accept"};
  check(fixtureHandoffSnapshotCalls==3&&fixtureAuthorityTrace==expectedAuthority&&fixtureNativeProofCalls==7&&std::count(fixtureNativeProofTargets.begin(),fixtureNativeProofTargets.end(),pathA)==3&&std::count(fixtureNativeProofTargets.begin(),fixtureNativeProofTargets.end(),pathB)==3&&std::count(fixtureNativeProofTargets.begin(),fixtureNativeProofTargets.end(),foreignPath)==1&&fixtureAcceptCalls==1&&!fixtureNativeDonorHeld,"batch v2 exact authority order is held mode, Snapshot before/precommit/postcommit, each donor per-path triple, foreign query, then accept");
  const auto atomicRecord=disk();
  check(operations.size()==2&&operations[0]["path"]==pathA&&operations[1]["path"]==pathB&&operations[0]["recordRevision"]==2&&operations[1]["recordRevision"]==2&&atomicRecord.record&&atomicRecord.record->revision==2&&atomicRecord.record->obligations.size()==1&&atomicRecord.record->obligations[0].paths.size()==2&&std::all_of(atomicRecord.record->obligations[0].paths.begin(),atomicRecord.record->obligations[0].paths.end(),[](const auto& path){return path.stage==duty::DispatchStage::Reserved;}),"batch v2 both exact hides reuse one atomically committed origin/revision 2; per-path reassertions add no revision and never fabricate Issued");
  check(world.count(pathA)&&world.count(pathB)&&world.count(foreignPath),"Apply leaves original hidden membership continuous without any unhide leaf");
  observeDebt("actual-Apply-produced-two-exact-duties");const auto prior=bytes();const auto owned=g_hidHideVisibilityJournal.hiddenByUs;
  HidHideVisibilitySnapshot stillHidden;hidHideCaptureVisibilitySnapshot(stillHidden);
  check(!settle(stillHidden,true,owned)&&bytes()==prior,"real AfterRestore refuses still-hidden readback without consuming any Apply debt");
  world.erase(pathA);world.erase(pathB);HidHideVisibilitySnapshot returned;hidHideCaptureVisibilitySnapshot(returned);
  check(settle(returned,true,owned)&&pathCount(disk())==0&&disk().record->obligations.empty(),"real AfterRestore clears every actual Apply duty after controlled complete owned return");
  check(world.count(foreignPath)&&!hidHideContainsPath(owned,foreignPath),"foreign prehidden is neither claimed nor returned");
  runtime::state=runtime::State{};auto reopened=duty::Session::open(fixtureRecordPath,fixtureMachineBinding());
  check(reopened.ready()&&reopened.record()->obligations.empty(),"actual Session reopen verifies AfterRestore settlement persisted");observeDebt("actual-AfterRestore-clean-record");
 });
 run("Apply/reservation-commit-failure-zero-hide",[]{prepareApply(true);seedEmpty();const auto prior=bytes();{FileLock held(fixtureRecordPath);check(!applyNative(),"actual Apply rejects real atomic replacement failure");}check(bytes()==prior&&operations.empty()&&pathCount(disk())==0&&g_hidHideVisibilityJournal.hiddenByUs.empty(),"failed reservation causes zero hide and no claimed path");check(fixtureNativeDonorHeld&&fixtureAcceptCalls==0,"failed reservation retains modeled native donor");});
 run("Apply/authority-retires-at-BeforeHide-zero-hide",[]{prepareApply(true);fixtureNativeProofExpireAt=2;check(!applyNative(),"actual Apply cannot bypass real per-path BeforeHide donor authority recheck after batch reservation");const auto record=disk();check(fixtureNativeProofCalls==2&&operations.empty()&&pathCount(record)==2&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA)&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathB)&&g_hidHideVisibilityJournal.hiddenByUs.size()==2&&fixtureNativeDonorHeld&&fixtureAcceptCalls==0,"batch v2 per-path authority expiry emits zero hide while preserving complete prior A+B durable and journal responsibility");observeDebt("batch-reserved-before-perpath-authority-retirement");});
 run("Apply/commit-then-authority-retires-exact-debt",[]{prepareApply(true);fixtureAfterCommit=[]{fixtureNativeDonorProof=false;};check(!applyNative()&&fixtureCommitObserved,"actual Apply rejects authority lost after real atomic full-set Session commit");retainExactDebt({},2);});
 run("Apply/commit-then-context-retires-exact-debt",[]{prepareApply(true);fixtureAfterCommit=[]{fixtureClock=g_inputHostSwitchRecoveryStartDeadline;};check(!applyNative()&&fixtureCommitObserved,"actual Apply rejects original context expiring after real atomic full-set Session commit");retainExactDebt({},2);const auto owned=g_hidHideVisibilityJournal.hiddenByUs;world.erase(pathA);world.erase(pathB);HidHideVisibilitySnapshot returned;hidHideCaptureVisibilitySnapshot(returned);check(settle(returned,true,owned)&&pathCount(disk())==0&&disk().record->obligations.empty()&&world.count(foreignPath),"original AfterRestore can pay EVERY committed refused-dispatch batch member after context retirement while preserving foreign");});
 run("Apply/unknown-ancestry-zero-hide",[]{prepareApply();fixtureProofState=duty::physicalPath::State::Unknown;check(!applyNative()&&operations.empty()&&pathCount(disk())==0,"actual Apply preserves real BeforeHide ancestry refusal despite donor model authority");});
 run("Apply/foreign-prehidden-no-claim",[]{g_hidHideVisibilityJournal={};world={pathA,foreignPath};fixtureTargets={pathA,foreignPath};check(applyNative(),"original Apply admits existing foreign configuration without claiming it");check(operations.empty()&&g_hidHideVisibilityJournal.hiddenByUs.empty()&&disk().state==duty::LoadState::Missing&&!runtime::state.session,"foreign prehidden creates no hide, journal ownership or durable record");});
 run("Apply/lost-hide-ACK-retains-exact-debt",[]{prepareApply(true);pipeOk=cliOk=false;check(!applyNative()&&operations.size()==2,"actual Apply executes original bounded pipe-to-CLI fallback on lost hide ACK");check(pathCount(disk())==2&&g_hidHideVisibilityJournal.hiddenByUs.size()==2&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathB)&&fixtureNativeDonorHeld&&fixtureAcceptCalls==0,"lost ACK on A retains whole real A+B durable and journal batch without modeled donor transfer or visiting B hide");check(world.count(pathA)&&world.count(foreignPath),"lost hide ACK has no unhide gap");observeDebt("actual-Apply-ACK-loss-debt");});
 run("Apply/failed-readback-retains-exact-debt",[]{prepareApply();fixtureSnapshotFailAt=2;check(!applyNative()&&operations.size()==1,"actual Apply refuses failed final visibility readback after original hide");check(pathCount(disk())==1&&g_hidHideVisibilityJournal.hiddenByUs.size()==1&&fixtureNativeDonorHeld&&fixtureAcceptCalls==0,"failed readback retains committed owned responsibility without donor transfer");});
 run("Apply/reopened-duty-through-actual-coldExecution-run",[]{
  prepareApply(true);check(applyNative()&&pathCount(disk())==2,"cold execution input is produced by actual Apply and actual BeforeHide reservations");
  const auto produced=*disk().record;const auto prior=bytes();observeDebt("actual-Apply-before-in-memory-loss");
  runtime::state=runtime::State{};g_hidHideVisibilityJournal={};g_inputHostProcess=nullptr;g_inputHostHostInstanceId.clear();g_inputHostRunId.clear();
  check(bytes()==prior,"dropping all producer Session/runtime/journal ownership preserves original durable bytes");
  ApplyColdEnvironment env(produced);const auto result=duty::coldExecution::run(env);
  check(result.clean()&&result.unhideAttempts==2&&result.settledPaths==2&&env.unhideCalls==2&&env.consumeCalls==2,"actual coldExecution::run consumes both exact actual Apply paths through real Session commits");
  check(env.record()->obligations.empty()&&world.size()==1&&world.count(foreignPath),"cold execution clears all owned debt and preserves foreign prehidden");
  events.push_back({{"event","actual-coldExecution-result"},{"reason",duty::coldExecution::reasonName(result.reason)},{"unhideAttempts",result.unhideAttempts},{"settledPaths",result.settledPaths},{"settledOrigins",result.settledOrigins},{"controlledEnvironmentTrace",env.trace}});
  env.session.reset();auto reopened=duty::Session::open(fixtureRecordPath,fixtureMachineBinding());
  check(reopened.ready()&&reopened.record()->obligations.empty()&&reopened.record()->revision>produced.revision,"second real Session reopen verifies cold execution settlement is durable");observeDebt("actual-coldExecution-reopened-clean-record");
 });
}
#endif
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;fixtureCaseRoot=argv[1];fspath::create_directories(fixtureCaseRoot);
#ifdef YMCC_APPLY_INTEGRATION_ONLY
#ifdef YMCC_APPLY_INTERRUPT_ONLY
 applyInterruptCase();
#else
 applyIntegrationCases();
 applyBatchAuthorityCases();
#endif
#else
 run("Refresh/reserve-before-driver",[]{check(refresh(),"real Refresh accepts successful hide after durable reservation");const auto loaded=disk();check(operations.size()==1&&pathCount(loaded)==1,"single hide creates one exact physical debt");const auto* owner=pathOwner(loaded,pathA);check(owner&&owner->origin.physicalRoot==rootId&&owner->origin.host.pid==700001,"durable origin pins selected root and modeled old Host");check(runtime::state.physicals.size()==1,"actual physical producer publishes one owned in-process origin");});
 run("Refresh/ACK-loss-retains-debt",[]{pipeOk=cliOk=false;check(!refresh(),"lost hide receipts retain failed actual Refresh result");check(world.count(pathA)==1&&operations.size()==2,"pipe then bounded CLI both may apply despite lost receipts");check(pathCount(disk())==1&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA),"lost hide ACK never deletes durable or local return debt");});
 run("Refresh/CLI-fallback-reserved-once",[]{pipeOk=false;check(refresh(),"actual original fallback accepts CLI receipt");check(operations.size()==2&&pathCount(disk())==1,"fallback never creates a second path origin");});
 run("Reassert/ordinary-foreign-prehidden-still-rejected",[]{
  seedNativeDonor();check(!reassertNativeOwned(false),"ordinary Before rejects prehidden even if native leaf would attest");
  check(fixtureNativeProofCalls==0&&operations.empty()&&disk().state==duty::LoadState::Missing,"ordinary path does not query donor or acquire disk ownership");
 });
 run("Reassert/current-not-hidden-rejected",[]{
  fixtureNativeDonorProof=true;check(!reassertNativeOwned(),"native reassert entry requires current hidden membership");
  check(fixtureNativeProofCalls==0&&operations.empty()&&pathCount(disk())==0,"reassert flag cannot create a new ordinary hide");
 });
 run("Reassert/native-proof-false",[]{
  world.insert(pathA);check(!reassertNativeOwned(),"true reassert flag is insufficient when actual authority leaf refuses");
  check(fixtureNativeProofCalls==1&&operations.empty()&&disk().state==duty::LoadState::Missing,"proof refusal issues neither reservation nor hide");
 });
 for(unsigned invalid=0;invalid<3;++invalid){const auto name="Reassert/invalid-donor-baseline-"+std::to_string(invalid);run(name.c_str(),[=]{
  seedNativeDonor();if(invalid==0)fixtureNativeDonorBaseline.valid=false;
  else if(invalid==1)fixtureNativeDonorBaseline.hidden={U2W(pathA)};
  else fixtureNativeDonorPath=pathB;
  check(!reassertNativeOwned()&&operations.empty()&&pathCount(disk())==0,"invalid baseline or wrong exact donor path refuses prehidden reservation");
  check(fixtureNativeDonorHeld&&world.count(pathA)==1&&g_hidHideVisibilityJournal.hiddenByUs.empty(),"refusal preserves original native responsibility and hidden state");
 });}
 run("Reassert/durable-before-same-hide-idempotent-and-cold-readable",[]{
  seedNativeDonor();check(reassertNativeOwned(),"actual Before reserves prehidden native-owned path before same hide leaf");
  const auto saved=disk();const auto* group=pathOwner(saved,pathA);
  check(saved.record&&saved.record->schema==1&&pathCount(saved)==1&&group,"reassert uses unchanged schema and one exact existing path reservation");
  const auto first=bytes();check(reassertNativeOwned(),"same original donor may reassert same hide idempotently");
  check(bytes()==first&&pathCount(disk())==1&&operations.size()==2,"second hide retains exact origin/revision without double claiming path");
  check(fixtureNativeProofCalls==4&&world.count(pathA)==1,"each real hide boundary checks donor before and after commit without unhide");
  runtime::state=runtime::State{};
  bool reopened=false;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);reopened=runtime::open();}
  check(reopened&&runtime::state.coldHistoryPending&&runtime::state.physicals.empty(),"new runtime loads durable physical debt as cold history rather than live admission");
  check(bytes()==first&&runtime::state.session&&runtime::state.session->record()&&runtime::state.session->record()->obligations.size()==1,"actual new Session parses original schema and preserves cold recovery record");
  const auto loaded=disk();check(pathOwner(loaded,pathA)!=nullptr&&pathCount(loaded)==1,"same exact physical path is readable after dropping all in-memory ownership");
  check(!reassertNativeOwned()&&bytes()==first&&operations.size()==2,"historical file alone cannot authorize a new reassert origin after runtime reset");
 });
 for(const auto proof:{duty::physicalPath::State::Unknown,duty::physicalPath::State::Rejected}){const auto name=proof==duty::physicalPath::State::Unknown?"Reassert/unknown-ancestry":"Reassert/rejected-ancestry";run(name,[=]{
  seedNativeDonor();fixtureProofState=proof;
  check(!reassertNativeOwned()&&operations.empty()&&pathCount(disk())==0,"native donor authority does not bypass exact ancestry proof");
 });}
 run("Reassert/commit-failure-retains-native-owner",[]{
  seedNativeDonor();seedEmpty();const auto prior=bytes();
  {FileLock locked(fixtureRecordPath);check(!reassertNativeOwned(),"actual atomic commit failure prohibits re-hide dispatch");}
  check(bytes()==prior&&pathCount(disk())==0&&operations.empty(),"failed write does not publish fake durable reservation");
  check(fixtureNativeDonorHeld&&world.count(pathA)==1&&g_hidHideVisibilityJournal.hiddenByUs.empty(),"original native debt survives failed durable reservation without adoption");
 });
 run("Reassert/commit-then-context-expires-retains-duty",[]{
  seedNativeDonor();fixtureAfterCommit=[]{fixtureClock=g_inputHostSwitchRecoveryStartDeadline;};
  check(!reassertNativeOwned()&&fixtureCommitObserved,"commit can complete before original request expires");
  check(operations.empty()&&pathCount(disk())==1&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA),"expired context retains committed and journal duty without re-hide");
  check(fixtureNativeDonorHeld&&world.count(pathA)==1,"context expiry cannot transfer away native donor responsibility");
 });
 run("Reassert/commit-then-donor-proof-expires-retains-duty",[]{
  seedNativeDonor();fixtureAfterCommit=[]{fixtureNativeDonorProof=false;};
  check(!reassertNativeOwned()&&fixtureNativeProofCalls==2,"native proof must still hold after actual Session commit");
  check(operations.empty()&&pathCount(disk())==1&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA),"lost donor proof withholds dispatch but preserves durable/journal responsibility");
 });
 for(unsigned field=0;field<4;++field){const auto name="Reassert/commit-then-donor-baseline-changed-"+std::to_string(field);run(name.c_str(),[=]{
  seedNativeDonor();fixtureAfterCommit=[=]{if(field==0)fixtureNativeDonorBaseline.hidden.push_back(U2W(pathB));
   else if(field==1)fixtureNativeDonorBaseline.cloaking=true;
   else if(field==2)fixtureNativeDonorBaseline.applications.push_back(L"fixture-app");else fixtureNativeDonorBaseline.inverseRaw="on";};
  check(!reassertNativeOwned()&&fixtureNativeProofCalls==2,"baseline must remain exact, not merely still omit target, across commit");
  check(operations.empty()&&pathCount(disk())==1&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA),"changed donor baseline preserves committed duty and suppresses late hide");
 });}
 run("Reassert/commit-then-power-changes-retains-duty",[]{
  seedNativeDonor();fixtureAfterCommit=[]{++fixturePower;};
  check(!reassertNativeOwned()&&operations.empty()&&pathCount(disk())==1,"native reassert retains original generation gate after durable commit");
  check(hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA),"changed power publishes responsibility out to caller journal");
 });
 run("Reassert/lost-hide-ACK-does-not-consume-duty",[]{
  seedNativeDonor();pipeOk=false;
  check(!reassertNativeOwned()&&operations.size()==1&&world.count(pathA)==1,"same hide may take effect with lost modeled ACK and never unhide");
  check(pathCount(disk())==1&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA),"lost hide ACK retains durable and caller journal responsibility");
  HidHideVisibilitySnapshot after;hidHideCaptureVisibilitySnapshot(after);
  check(!settle(after,true,{U2W(pathA)})&&pathCount(disk())==1,"readback still hidden cannot settle original durable debt");
 });
 run("Refresh/prehidden-foreign-not-claimed",[]{world.insert(pathA);g_hidHideVisibilityJournal.before.hidden={U2W(pathA)};check(refresh(),"prehidden foreign configuration preserves original refresh result");check(operations.empty()&&runtime::state.physicals.empty()&&!runtime::state.session,"prehidden foreign path never opens physical record or claims ownership");check(disk().state==duty::LoadState::Missing&&g_hidHideVisibilityJournal.hiddenByUs.empty(),"prehidden foreign path creates neither durable nor local debt");});
 for(auto proof:{duty::physicalPath::State::Unknown,duty::physicalPath::State::Rejected}){const auto name=proof==duty::physicalPath::State::Unknown?"Before/root-unknown-zero-hide":"Before/root-rejected-zero-hide";run(name,[=]{fixtureProofState=proof;check(!refresh()&&operations.empty()&&pathCount(disk())==0,"unproven ancestry cannot reserve or dispatch physical hide");check(g_hidHideVisibilityJournal.hiddenByUs.empty(),"unproven ancestry does not invent local responsibility");});}
 for(unsigned field=0;field<4;++field){const auto name="Before/changed-generation-"+std::to_string(field);run(name.c_str(),[=]{fixtureAfterProof=[=]{if(field==0)++g_hidResolveCacheGeneration;else if(field==1)++g_xinputTopologyEpoch;else if(field==2)++g_msiMainSourceRevision;else ++fixturePower;};check(!refresh()&&operations.empty()&&pathCount(disk())==0,"changed source identity topology or power during proof dispatches zero hide");});}
 run("Before/changed-owner-during-proof",[]{fixtureAfterProof=[]{g_inputHostRunId="other-run";++g_inputHostEpoch;};check(!refresh()&&operations.empty()&&pathCount(disk())==0,"changed Host tuple after ancestry proof cannot reserve or dispatch");});
 run("Before/expired-original-deadline",[]{fixtureClock=g_inputHostSwitchRecoveryStartDeadline;check(!refresh()&&fixtureProofCalls==0&&operations.empty(),"expired original request cannot even begin physical proof");});
 run("Before/unowned-mutex-no-session",[]{bool reserved=true;HidHideVisibilitySnapshot now=absent();check(!inputHostTargetReturnBeforeHideLocked(fixtureSources,now,g_hidHideVisibilityJournal.before,pathA,"journal-1",&reserved),"actual Before requires real TransactionMutex ownership");check(!reserved&&!runtime::state.session&&operations.empty(),"unowned mutex leaves Session unopened and responsibility false");});
 run("After/unowned-mutex-no-consume",[]{check(refresh(),"owned setup reserves actual physical path");const auto prior=bytes();check(!inputHostTargetReturnAfterPhysicalRestoreLocked(absent(),true,{U2W(pathA)}),"actual After refuses unlocked caller");check(bytes()==prior&&pathCount(disk())==1,"unowned After cannot consume disk duty");});
 run("Before/corrupt-file-zero-hide",[]{std::ofstream(fspath::path(fixtureRecordPath),std::ios::binary)<<"{broken-111";check(!refresh()&&operations.empty(),"corrupt record rejects physical hide before either driver leaf");check(bytes()=="{broken-111"&&g_hidHideVisibilityJournal.hiddenByUs.empty(),"corrupt historical bytes never replaced with empty accepted record");});
 run("Before/owner-conflict-zero-hide",[]{const auto owner=CreateFileW((fixtureRecordPath+L".owner").c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(owner==INVALID_HANDLE_VALUE)throw std::runtime_error("private exclusive owner setup failed");const bool accepted=refresh();CloseHandle(owner);check(!accepted&&operations.empty()&&pathCount(disk())==0,"real exclusive session conflict prevents any hide dispatch");});
 run("Before/commit-store-failure-zero-hide",[]{seedEmpty();const auto prior=bytes();{FileLock locked(fixtureRecordPath);check(!refresh()&&operations.empty(),"real atomic replace failure refuses physical hide");}check(bytes()==prior&&pathCount(disk())==0&&g_hidHideVisibilityJournal.hiddenByUs.empty(),"failed reservation commit preserves both exact disk and unclaimed journal");});
 run("Before/commit-then-context-expires",[]{fixtureAfterCommit=[]{fixtureClock=g_inputHostSwitchRecoveryStartDeadline;};check(!refresh()&&fixtureCommitObserved,"real persistence can complete before original context retires");check(operations.empty()&&pathCount(disk())==1&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA),"commit then retired context retains durable and local responsibility but dispatches zero hide");});
 run("Before/direct-reserved-out-after-retirement",[]{fixtureAfterCommit=[]{fixtureClock=g_inputHostSwitchRecoveryStartDeadline;};bool reserved=false;bool accepted=false;{std::lock_guard<InputHostMutex> lock(g_inputHostMx);accepted=inputHostTargetReturnBeforeHideLocked(fixtureSources,absent(),g_hidHideVisibilityJournal.before,pathA,"journal-1",&reserved);}check(!accepted&&reserved&&pathCount(disk())==1,"actual bool out reports committed responsibility even when return is false after context expiry");});
 run("Carry/same-path-new-Host-preserves-old-origin",[]{check(refresh(),"carry setup commits original physical path");const auto old=disk();const auto* oldGroup=pathOwner(old,pathA);if(!oldGroup)throw std::runtime_error("missing old origin");const auto original=oldGroup->origin;const auto revision=old.record->revision;world.clear();operations.clear();g_inputHostProcess=reinterpret_cast<HANDLE>(std::uintptr_t{2});fixtureHostPid=700002;g_inputHostHostInstanceId="owned-host-2";g_inputHostRunId="owned-run-2";g_inputHostEpoch=2;g_hidHideVisibilityJournal.transactionId="journal-2";g_hidHideVisibilityJournal.before.hidden={U2W(pathA)};g_hidHideVisibilityJournal.hiddenByUs.clear();check(refresh(),"actual carried local responsibility can rehide under new Host without resetting state");const auto next=disk();const auto* group=pathOwner(next,pathA);check(group&&duty::sameOrigin(group->origin,original)&&next.record->revision==revision&&pathCount(next)==1,"same path carry preserves exact original parent Host tuple origin and revision");check(group&&group->origin.host.pid==700001&&runtime::state.physicals.size()==1,"new Host never rewrites prior physical ownership");});
 run("Before/historical-file-alone-not-authority",[]{duty::Record record;record.machineBinding="fixture-machine";duty::Origin origin;origin.transactionId="historical-111";origin.parent={800001,"132000000000000101"};origin.host={800002,"132000000000000102"};origin.hostInstanceId="historical-host";origin.runId="historical-run";origin.targetId=kInputHostTargetId;origin.persona="steamdeck";origin.physicalRoot=rootId;origin.epoch=1;duty::Record candidate;if(!duty::reservePath(record,record.revision,origin,pathA,{true,true,true},candidate))throw std::runtime_error("history reservation setup failed");check(duty::store(fixtureRecordPath,candidate).committed(),"private historical physical debt seeded by actual record/store");const auto prior=bytes();check(!refresh()&&operations.empty(),"loaded historical same path cannot become fresh in-process hide authority");check(bytes()==prior&&runtime::state.physicals.empty(),"file-only obligation remains historical without ownership import");});
 run("Before/ambiguous-two-selected-roots",[]{auto second=fixtureSources[0];second.baseContainerDeviceInstanceId="USB\\VID_1111&PID_2222\\OTHER";fixtureSources.push_back(second);check(!refresh()&&operations.empty()&&pathCount(disk())==0,"two different proven selected roots cannot manufacture one physical owner");});
 run("Before/original-baseline-unknown",[]{g_hidHideVisibilityJournal.before.valid=false;check(!refresh()&&operations.empty()&&pathCount(disk())==0,"missing absent-before baseline cannot authorize new physical reservation");});
 run("Before/original-prehidden-but-current-visible",[]{g_hidHideVisibilityJournal.before.hidden={U2W(pathA)};check(!refresh()&&operations.empty()&&pathCount(disk())==0,"newly visible but originally foreign-hidden path is not ours to claim");});
 for(unsigned unknown=0;unknown<3;++unknown){const auto name="After/unknown-or-still-hidden-"+std::to_string(unknown);run(name.c_str(),[=]{check(refresh(),"return negative setup reserves real physical debt");const auto prior=bytes();auto after=absent();bool readback=true;if(unknown==0)readback=false;else if(unknown==1)after.valid=false;else after.hidden={U2W(pathA)};check(!settle(after,readback,{U2W(pathA)}),"unknown readback invalid snapshot or still-hidden path cannot consume physical responsibility");check(bytes()==prior&&pathCount(disk())==1&&runtime::state.physicals.size()==1,"uncertain return preserves exact durable record and local physical origin");});}
 run("After/absent-consumes-exact-path",[]{check(refresh(),"complete return setup reserves real path");check(settle(absent(),true,{U2W(pathA)}),"actual known absent owned path settles successfully");check(pathCount(disk())==0&&runtime::state.physicals.empty(),"actual Session consumes exact confirmed path and prunes empty physical origin");});
 run("After/owned-scope-does-not-consume-other-path",[]{check(refresh(),"scope setup creates original debt");const auto prior=bytes();check(settle(absent(),true,{U2W(pathB)}),"unrelated owned-target scope may complete without changing our debt");check(bytes()==prior&&pathCount(disk())==1,"absent unrelated path is not proof our recorded path returned");});
 run("After/store-failure-retains-then-retries",[]{check(refresh(),"settlement failure setup reserves real path");if(!runtime::state.session||!runtime::state.session->record())throw std::runtime_error("missing real Session setup under semantic control");const auto prior=bytes(),memory=encoded(*runtime::state.session->record());{FileLock locked(fixtureRecordPath);check(!settle(absent(),true,{U2W(pathA)}),"actual store failure prevents reporting physical settlement success");check(bytes()==prior&&encoded(*runtime::state.session->record())==memory&&pathCount(disk())==1&&runtime::state.physicals.size()==1,"failed consume commit preserves both exact disk and Session data plus origin");}check(settle(absent(),true,{U2W(pathA)})&&pathCount(disk())==0,"same owned debt settles on later confirmed retry without resetting runtime");});
 run("After/two-paths-consume-only-exact-confirmed-one",[]{fixtureTargets={pathA,pathB};check(refresh()&&pathCount(disk())==2,"two reached paths reserve two exact debts in same origin");auto after=absent();after.hidden={U2W(pathB)};check(settle(after,true,{U2W(pathA)})&&pathCount(disk())==1,"first proven returned path consumes only itself");check(pathOwner(disk(),pathB)!=nullptr,"other hidden path remains owed under original origin");check(!settle(after,true,{U2W(pathB)})&&pathCount(disk())==1,"later still-hidden exact path cannot be consumed");check(settle(absent(),true,{U2W(pathB)})&&pathCount(disk())==0,"remaining path can later settle without reconstructing ownership");});
 run("Refresh/failed-first-batch-tail-unattempted",[]{fixtureTargets={pathA,pathB};pipeOk=cliOk=false;check(!refresh()&&operations.size()==2&&pathCount(disk())==1,"only reached first path is persisted before batch stops");check(!pathOwner(disk(),pathB)&&!hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathB),"unattempted second path is never claimed");});
 run("Before/pending-native-CLI-model-zero-hide",[]{PendingWriterModel pending;check(!refresh()&&operations.empty()&&pathCount(disk())==0,"possible old native CLI writer refuses a new physical hide before Session commit");pending.signal();check(refresh()&&pathCount(disk())==1,"confirmed signaled CLI model releases only the existing writer-quiescence gate");});
 run("Before/transport-fault-zero-hide",[]{g_inputHostTransportFault=true;check(!refresh()&&operations.empty()&&pathCount(disk())==0,"transport-faulted modeled Host cannot dispatch another physical hide");});
 run("After/pending-native-CLI-model-retains",[]{check(refresh(),"old writer return setup commits path");const auto prior=bytes();{PendingWriterModel pending;check(!settle(absent(),true,{U2W(pathA)})&&bytes()==prior,"unproven native CLI writer prevents physical debt consumption despite absent readback");pending.signal();check(settle(absent(),true,{U2W(pathA)})&&pathCount(disk())==0,"same actual After settles after existing writer wait proves signaled");}});
 run("After/transport-fault-retains",[]{check(refresh(),"faulted writer return setup commits path");const auto prior=bytes();g_inputHostTransportFault=true;check(!settle(absent(),true,{U2W(pathA)})&&bytes()==prior,"faulted current Host retains durable physical debt despite absent readback");g_inputHostTransportFault=false;check(settle(absent(),true,{U2W(pathA)})&&pathCount(disk())==0,"same owned flow may settle once transport writer is quiescent");});
 for(const auto observed:{duty::process::State::Alive,duty::process::State::Unknown}){const auto name=observed==duty::process::State::Alive?"After/carried-old-Host-alive":"After/carried-old-Host-unknown";run(name,[=]{check(refresh(),"old Host physical duty committed before new Host binding");const auto prior=bytes();fixtureHostPid=700002;g_inputHostProcess=reinterpret_cast<HANDLE>(std::uintptr_t{2});g_inputHostHostInstanceId="owned-host-2";g_inputHostRunId="owned-run-2";g_inputHostEpoch=2;fixtureWriterState=observed;check(!settle(absent(),true,{U2W(pathA)})&&bytes()==prior&&pathCount(disk())==1,"carried old Host Alive or Unknown cannot consume its original physical debt");check(fixtureWriterProbes.size()==1&&fixtureWriterProbes[0].pid==700001&&fixtureWriterProbes[0].creationTime=="132000000000000001","process observation targets exact old modeled owner identity rather than new Host or parent");fixtureWriterState=duty::process::State::Dead;check(settle(absent(),true,{U2W(pathA)})&&pathCount(disk())==0,"confirmed old owner Dead permits same recorded-path settlement without state reset");});}

 // Task124 scope additions. They exercise real writer file Session and bridge
 // algorithms; only exact old-parent/child/job observations are controlled.
 run("CLI-writer/owner-lock-gates-before-and-after",[]{
  {auto held=ymcc::hidhideCliWriter::Session::open(fixtureCliWriterPath,"fixture-cli-machine");check(held.ready(),"independent real writer Session owns private zero-byte sentinel");check(!refresh()&&operations.empty()&&pathCount(disk())==0,"held writer owner prevents Before dispatch/reservation");}
  check(refresh()&&pathCount(disk())==1,"releasing real writer owner permits existing physical path flow");const auto before=bytes();
  {auto held=ymcc::hidhideCliWriter::Session::open(fixtureCliWriterPath,"fixture-cli-machine");check(held.ready(),"independent real writer owner reacquired");check(!settle(absent(),true,{U2W(pathA)})&&bytes()==before,"held writer owner prevents After consumption");}
  check(settle(absent(),true,{U2W(pathA)})&&pathCount(disk())==0,"same physical responsibility settles after owner release");
 });
 auto seedOldCli=[] {
  auto session=ymcc::hidhideCliWriter::Session::open(fixtureCliWriterPath,"fixture-cli-machine");
  ymcc::hidhideCliWriter::Attempt attempt;attempt.attemptId=fixtureCliAttemptId;attempt.parent=fixtureCliOldParent;attempt.jobName=std::string(ymcc::hidhideCliWriter::jobPrefix)+attempt.attemptId;
  if(!session.ready()||!session.reserve(session.record()->revision,attempt)||!session.markMayRun(session.record()->revision,attempt.attemptId,fixtureCliOldChild))throw std::runtime_error("actual old-writer file seed failed");
 };
 run("CLI-writer/old-child-and-job-gated",[&]{
  seedOldCli();fixtureCliChildState=duty::process::State::Alive;
  check(!refresh()&&operations.empty()&&pathCount(disk())==0,"old child Alive prevents physical mutation");
  fixtureCliChildState=duty::process::State::Unknown;
  check(!refresh()&&operations.empty(),"old child Unknown prevents physical mutation");
  fixtureCliChildState=duty::process::State::Dead;fixtureCliJobState=ymcc::hidHideCliWriterRuntime::JobState::Active;
  check(!refresh()&&operations.empty(),"old subtree Active prevents physical mutation");
  fixtureCliJobState=ymcc::hidHideCliWriterRuntime::JobState::Unknown;
  check(!refresh()&&operations.empty(),"unknown old subtree does not become idle proof");
  const auto saved=ymcc::hidhideCliWriter::load(fixtureCliWriterPath,"fixture-cli-machine");
  check(saved.record&&saved.record->active&&saved.record->revision==3,"all deferrals preserve the exact original active writer record");
  check(fixtureForbiddenIo==0,"only explicitly registered old identities/named job were probed");
 });
 run("CLI-writer/dead-idle-clears-slot-then-physical",[&]{
  seedOldCli();fixtureCliParentState=fixtureCliChildState=duty::process::State::Dead;fixtureCliJobState=ymcc::hidHideCliWriterRuntime::JobState::Idle;
  check(refresh()&&pathCount(disk())==1,"actual clearSettled commits old writer exit then permits original physical Before");
  const auto saved=ymcc::hidhideCliWriter::load(fixtureCliWriterPath,"fixture-cli-machine");
  check(saved.record&&!saved.record->active&&saved.record->revision==4,"real writer Session/store settles exact slot once");
  check(fixtureCliProbes.size()==2&&fixtureCliJobProbes==1,"exact old parent/child and named job observed once before settlement");
  check(settle(absent(),true,{U2W(pathA)})&&pathCount(disk())==0,"actual After continues through empty real writer record");
 });
 run("CLI-writer/same-native-reaper-releases-own-lease",[]{
  PendingWriterModel pending;
  auto session=std::unique_ptr<ymcc::hidhideCliWriter::Session>(new ymcc::hidhideCliWriter::Session(ymcc::hidhideCliWriter::Session::open(fixtureCliWriterPath,"fixture-cli-machine")));
  const auto parent=fixtureCapture(GetCurrentProcess());if(!session->ready()||!parent)throw std::runtime_error("same-native real lease seed failed");
  ymcc::hidhideCliWriter::Attempt attempt;attempt.attemptId=fixtureCliAttemptId;attempt.parent=*parent;attempt.jobName=std::string(ymcc::hidhideCliWriter::jobPrefix)+attempt.attemptId;
  if(!session->reserve(session->record()->revision,attempt)||!session->markMayRun(session->record()->revision,attempt.attemptId,fixtureCliOldChild))throw std::runtime_error("same-native actual writer reserve failed");
  g_hidHidePendingAttempt=attempt.attemptId;g_hidHidePendingWriter=std::move(session);
  check(!refresh()&&operations.empty()&&g_hidHidePendingWriter,"unproven model root retains exact live lease without self-open");
  pending.signal();
  check(refresh()&&pathCount(disk())==1,"actual reaper settles/releases retained lease before actual quiescent reopens it");
  check(!g_hidHidePendingChildProcess&&!g_hidHidePendingWriter&&g_hidHidePendingAttempt.empty(),"actual pending root/writer/attempt are cleared by original reaper");
  const auto saved=ymcc::hidhideCliWriter::load(fixtureCliWriterPath,"fixture-cli-machine");
  check(saved.record&&!saved.record->active&&saved.record->revision==4,"exact pending writer slot cleared with one real commit");
  auto reopened=ymcc::hidhideCliWriter::Session::open(fixtureCliWriterPath,"fixture-cli-machine");
  check(reopened.ready()&&fixtureCliProbes.empty()&&fixtureCliJobProbes==0,"no held-owner self-deadlock or foreign liveness probe remains");
 });

#endif
 json result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",checks},{"cases",cases},{"failures",failures},{"actualRefreshRuntimeSessionStore",true},{"actualCliWriterSessionStoreBridgeAndReaper",true},{"actualJobGuardClassCompiled",true},{"realJobPreparedOrQueried",false},{"cliFiveEnvironmentLeavesModeled",true},{"pendingRootIsExplicitEventModel",true},{"unregisteredCliProbeIo",fixtureForbiddenIo},{"realPrivateFileIo",true},{"realTransactionMutex",true},{"physicalDiscoveryAndCmProof","controlled leaf responses only"},{"hostOwner","explicit synthetic native token; real current parent capture"},{"realDeviceApisOrProductHostExecuted",false},{"coldRecoveryOrHardwareAccepted",false}};
#ifdef YMCC_APPLY_INTEGRATION_ONLY
 result["integrationAssertionContract"]="batch-v2: native existing-owned set is Reserved in one atomic revision before any re-hide; per-path hooks reuse origin; false after commit publishes whole journal; original fault requirements retained";
 result["consumer"]="actual source-exact hidHideApplyPhysicalTransaction -> actual BeforeHide/Session/store -> actual AfterRestore; separate reopened Apply duty -> actual coldExecution::run";
 result["actualApplyConsumerCompiledAndCalled"]=true;result["actualAfterRestoreCalled"]=true;result["actualColdExecutionRunCalled"]=true;
 result["actualRefreshRuntimeSessionStore"]=false;result["nativeDonorAuthorityIsExplicitFakeLeaf"]=true;result["realNativeDonorStateMachineProven"]=false;
#ifdef YMCC_APPLY_INTERRUPT_ONLY
 result["actualAfterRestoreCalled"]=false;result["interruptionModel"]="CrashInterrupted at first actual hide leaf; no rollback; producer runtime/Session/journal/donor discarded; original disk and world retained";
 result["nativeInitialHideBeforeHandoffReservationCovered"]=false;result["recordTailSynthesizedByFixture"]=false;
#endif
 result["coldWriterDeathAbsenceAncestryAndUnhideAreControlled"]=true;result["actualProcessTerminated"]=false;result["productionColdCallerCompiled"]=false;
#endif
 std::ofstream(fspath::path(argv[2]),std::ios::binary)<<result.dump(2)<<"\n";std::cout<<json{{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",checks},{"failures",failures}}.dump()<<"\n";return failures.empty()?0:1;
}
