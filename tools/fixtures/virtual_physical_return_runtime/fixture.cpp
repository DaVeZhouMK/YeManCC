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
 if(event&&std::strcmp(event,"input-host-target-return-record")==0&&facts.value("phase",std::string{})=="physical-path-reserved"){
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
static duty::LoadResult disk(){return duty::load(fixtureRecordPath,"fixture-machine");}
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
static bool hidHideCaptureVisibilitySnapshot(HidHideVisibilitySnapshot& out){++snapshots;if(!snapshotOk)return false;out={};out.valid=true;out.cloaking=true;for(const auto& path:world)out.hidden.push_back(U2W(path));return true;}
static bool hidHideApplyPhysicalTransaction(const std::string&,bool){++applyFallback;return false;}
static std::vector<std::string> hidHideVirtualPhysicalTargets(const HidHidePhysicalIdentity&,const HidHideVisibilitySnapshot&,std::size_t,const char*){return fixtureTargets;}
static bool msiCrashRecoveryBeforeHide(const HidHidePhysicalIdentity&,const std::vector<std::string>&,const HidHideVisibilitySnapshot&,const std::string&){return reservationOk;}
static bool inputHostHidHideApplyViaPipeLocked(const std::wstring& action,const std::wstring& path){if(action!=L"hide")throw std::logic_error("unadmitted driver action");const auto value=W2U(path);driverObservation("pipe",value);if(pipeApplies)world.insert(value);return pipeOk;}
static bool hidHideRun(const std::wstring& args){const std::wstring prefix=L"--dev-hide \"";if(args.rfind(prefix,0)!=0||args.back()!=L'"')throw std::logic_error("unadmitted CLI action");const auto value=W2U(args.substr(prefix.size(),args.size()-prefix.size()-1));driverObservation("CLI",value);if(cliApplies)world.insert(value);return cliOk;}
static std::wstring hidHideCurrentModulePath(){return {};}
static bool msiControllerTraceEnabled(){return false;}
static json msiControllerVisibilityApplications(const HidHideVisibilitySnapshot&,bool){return json::object();}
static void msiControllerTraceLog(const char*,json,bool){}
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
static void run(const char* name,const std::function<void()>& body){const auto start=failures.size();try{reset(name);body();}catch(const std::exception& e){failures.push_back({{"case",name},{"exception",e.what()}});}if(fixtureForbiddenIo)failures.push_back({{"case",name},{"assertion","unregistered CLI model identity is forbidden I/O"}});cases.push_back({{"name",name},{"pass",start==failures.size()},{"model","controlled discovery, CM proof and native Host token; real Refresh/runtime/Session/store"},{"operations",operations},{"events",events}});}
struct FileLock {HANDLE handle=INVALID_HANDLE_VALUE;explicit FileLock(const std::wstring& file){handle=CreateFileW(file.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);if(handle==INVALID_HANDLE_VALUE)throw std::runtime_error("could not acquire private actual no-share-delete handle");}~FileLock(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}FileLock(const FileLock&)=delete;FileLock&operator=(const FileLock&)=delete;};
// Real private event supplies WAIT_TIMEOUT/signaled only as an explicit CLI
// process-status model. No CLI process is launched or target handle queried.
struct PendingWriterModel {HANDLE handle=nullptr;PendingWriterModel(){handle=CreateEventW(nullptr,TRUE,FALSE,nullptr);if(!handle)throw std::runtime_error("private writer event model failed");g_hidHidePendingChildProcess=handle;}~PendingWriterModel(){if(g_hidHidePendingChildProcess==handle){g_hidHidePendingChildProcess=nullptr;if(handle)CloseHandle(handle);} /* else actual reaper already closed the model root */}void signal(){if(!SetEvent(handle))throw std::runtime_error("writer event signal failed");}};
static void seedEmpty(){duty::Record record;record.machineBinding="fixture-machine";check(duty::store(fixtureRecordPath,record).committed(),"private empty record seeded through actual store");}
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;fixtureCaseRoot=argv[1];fspath::create_directories(fixtureCaseRoot);
 run("Refresh/reserve-before-driver",[]{check(refresh(),"real Refresh accepts successful hide after durable reservation");const auto loaded=disk();check(operations.size()==1&&pathCount(loaded)==1,"single hide creates one exact physical debt");const auto* owner=pathOwner(loaded,pathA);check(owner&&owner->origin.physicalRoot==rootId&&owner->origin.host.pid==700001,"durable origin pins selected root and modeled old Host");check(runtime::state.physicals.size()==1,"actual physical producer publishes one owned in-process origin");});
 run("Refresh/ACK-loss-retains-debt",[]{pipeOk=cliOk=false;check(!refresh(),"lost hide receipts retain failed actual Refresh result");check(world.count(pathA)==1&&operations.size()==2,"pipe then bounded CLI both may apply despite lost receipts");check(pathCount(disk())==1&&hidHideContainsPath(g_hidHideVisibilityJournal.hiddenByUs,pathA),"lost hide ACK never deletes durable or local return debt");});
 run("Refresh/CLI-fallback-reserved-once",[]{pipeOk=false;check(refresh(),"actual original fallback accepts CLI receipt");check(operations.size()==2&&pathCount(disk())==1,"fallback never creates a second path origin");});
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

 json result={{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",checks},{"cases",cases},{"failures",failures},{"actualRefreshRuntimeSessionStore",true},{"actualCliWriterSessionStoreBridgeAndReaper",true},{"actualJobGuardClassCompiled",true},{"realJobPreparedOrQueried",false},{"cliFiveEnvironmentLeavesModeled",true},{"pendingRootIsExplicitEventModel",true},{"unregisteredCliProbeIo",fixtureForbiddenIo},{"realPrivateFileIo",true},{"realTransactionMutex",true},{"physicalDiscoveryAndCmProof","controlled leaf responses only"},{"hostOwner","explicit synthetic native token; real current parent capture"},{"realDeviceApisOrProductHostExecuted",false},{"coldRecoveryOrHardwareAccepted",false}};
 std::ofstream(fspath::path(argv[2]),std::ios::binary)<<result.dump(2)<<"\n";std::cout<<json{{"pass",failures.empty()},{"caseCount",cases.size()},{"assertions",checks},{"failures",failures}}.dump()<<"\n";return failures.empty()?0:1;
}
