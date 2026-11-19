#define NOMINMAX
#include "native_refresh_writer_runtime.h"
#include "native_foreground_source_policy.h"
#include "virtual_target_return_physical_path.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <functional>
namespace fspath=std::filesystem;
using json=nlohmann::json;
namespace nw=ymcc::nativeRefreshWriter;
static std::wstring U2W(const std::string& s){return std::wstring(s.begin(),s.end());}
static std::string W2U(const std::wstring& s){return std::string(s.begin(),s.end());}
struct HidHideVisibilitySnapshot {bool valid=true;std::vector<std::wstring> hidden;};
static bool hidHideContainsPath(const std::vector<std::wstring>& a,const std::string& b){return std::find(a.begin(),a.end(),U2W(b))!=a.end();}
static void hidHideAppendTarget(std::vector<std::string>& a,const std::string& b){if(std::find(a.begin(),a.end(),b)==a.end())a.push_back(b);}
struct FakeMutex {bool held=true;bool ownedByCurrentThread()const{return held;}} g_inputHostMx;
enum class PowerLifecycle{Ready,Suspended};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_inputReady{true},g_msiNativeForegroundPowerHold{false},g_exitRequested{false},g_inputCaptureStop{false},g_inputHostPowerAdmissionRevoked{false},g_msiNativeForegroundCyclePending{false},g_postHideCycleOwned{false};
static std::uint64_t g_inputHostSwitchRecoveryStartDeadline=0,powerGeneration=1;
static std::atomic<std::uint64_t> g_inputHostVirtualIntentRevision{1};
static std::atomic<bool> g_externalPriorityVirtualSuspended{false},g_shortcutRecordingActive{false};
static std::uint64_t requestId=1;
static std::string configuredPersona="disabled";
static const std::string& inputHostConfiguredPersona(){return configuredPersona;}
static struct {struct View {std::uint64_t requestId;};View view(std::uint64_t)const{return {requestId};}} g_virtualStartProtection;
static HANDLE g_inputHostProcess=nullptr;
static bool g_inputHostHello=false,g_inputHostPrepared=false,g_inputHostPrepareMayHaveReachedHost=false,g_inputHostActive=false,g_inputHostNeutralized=false,g_inputHostReleaseQuarantined=false;
static struct {bool active=false;} g_hidHideVisibilityJournal;
static struct {bool required()const{return false;}} g_msiSteamDeckReleaseDebt;
static struct {bool retirementRequired()const{return false;}} g_psEndpointOwnership;
static struct {bool active=false,restorePending=false;HANDLE cycleProcess=nullptr;std::shared_ptr<nw::Session> nativeVisibilitySession;} g_msiNativeForegroundLease;
static bool inputHostExitFenceRaised(){return false;}
static bool msiForegroundIsolationBorrowBusy(){return false;}
static uint64_t currentPowerGeneration(){return powerGeneration;}
static uint64_t inputHostStartProtectionNow(){return 100;}
static auto profile=ymcc::nativeForeground::Profile::Gpd;
static auto nativeForegroundProfile(){return profile;}
static std::wstring recordPath,logDirectory;
static std::string rootId="USB\\VID_045E&PID_028E\\7&ABC&0&1";
static std::vector<std::string> locations={"PCIROOT(0)#USBROOT(0)#USB(1)"};
static std::vector<std::wstring> hidden;
static std::vector<json> events;
static int snapshotCalls=0,unhideCalls=0,ancestryCalls=0,absenceCalls=0;
static int failUnhideAt=0,failSnapshotAt=0;
static bool absence=true,ancestry=true,writersQuiet=true;
static std::function<void(int)> onSnapshot,onUnhide;
static std::wstring nativeRefreshWriterRecordPath(){return recordPath;}
static std::wstring app_data_dir(){return logDirectory;}
static std::string inputHostTargetReturnMachineBinding(){return "windows-machine-guid:private-cold-fixture";}
static std::vector<std::wstring> msiForegroundIsolationLocations(const std::string& root){std::vector<std::wstring> v;if(root==rootId)for(const auto& s:locations)v.push_back(U2W(s));return v;}
namespace ymcc::msi {inline bool samePhysicalLocation(const std::vector<std::wstring>& a,const std::vector<std::wstring>& b){return a==b;}}
static void appendNativeLifecycleLog(const char*,const json& j){events.push_back(j);}
static bool msiNativeForegroundSnapshot(HidHideVisibilitySnapshot& s){++snapshotCalls;if(onSnapshot)onSnapshot(snapshotCalls);s.valid=snapshotCalls!=failSnapshotAt;s.hidden=hidden;return s.valid;}
static bool msiNativeForegroundVisibility(const std::wstring& p,bool hide){if(hide)throw std::runtime_error("unexpected-hide");++unhideCalls;if(onUnhide)onUnhide(unhideCalls);if(unhideCalls==failUnhideAt)return false;hidden.erase(std::remove(hidden.begin(),hidden.end(),p),hidden.end());return true;}
namespace ymcc::inputHost::targetReturnRuntime {static bool physicalWritersQuiescent(){return writersQuiet;}}
struct AbsenceLeaf {bool value;bool absent()const{return value;}};
static AbsenceLeaf inputHostTargetReturnColdAbsence(const std::vector<std::string>&){++absenceCalls;return {absence};}
struct AncestryLeaf {bool value;ymcc::targetReturn::physicalPath::Reason reason=ymcc::targetReturn::physicalPath::Reason::RootMatched;CONFIGRET configurationReturn=CR_SUCCESS;bool proven()const{return value;}};
static AncestryLeaf inputHostTargetReturnVerifyPhysicalPath(const std::string&,const std::string&){++ancestryCalls;return {ancestry};}

#ifdef YMCC_NATIVE_VISIBILITY_EMPTY_TAIL_TEST
namespace ymcc::targetReturn::process {
static bool coldProbeUnknown=false;
static int coldProbeCalls=0;
static std::function<void()> coldProbeAction;
static Probe fixtureColdProbe(const ProcessIdentity& identity){
    ++coldProbeCalls;
    auto result=probeRecordedOwner(identity); // default is the original Win32 prober
    if(coldProbeAction)coldProbeAction();
    if(coldProbeUnknown)return {State::Unknown,Reason::OpenFailed,ERROR_ACCESS_DENIED};
    return result;
}
}
#define probeRecordedOwner fixtureColdProbe
#endif
#include "candidate_cold.h"
#define nativeVisibilityReturn baselineNativeVisibilityReturn
#define inputHostNativeVisibilityColdReturnLocked baselineInputHostNativeVisibilityColdReturnLocked
#define inputHostNativeVisibilityHistoryPending baselineInputHostNativeVisibilityHistoryPending
#include "baseline_cold.h"
#undef inputHostNativeVisibilityHistoryPending
#undef inputHostNativeVisibilityColdReturnLocked
#undef nativeVisibilityReturn


#ifdef YMCC_NATIVE_VISIBILITY_EMPTY_TAIL_TEST
#define nativeVisibilityReturn beforeEmptyTailReturn
#define inputHostNativeVisibilityColdReturnLocked beforeEmptyTailColdReturn
#define inputHostNativeVisibilityHistoryPending beforeEmptyTailPending
#include "before_empty_tail_cold.h"
#undef inputHostNativeVisibilityHistoryPending
#undef inputHostNativeVisibilityColdReturnLocked
#undef nativeVisibilityReturn
#define nativeVisibilityReturn currentRemovedEmptyTailReturn
#define inputHostNativeVisibilityColdReturnLocked currentRemovedEmptyTailColdReturn
#define inputHostNativeVisibilityHistoryPending currentRemovedEmptyTailPending
#include "current_removed_empty_tail_cold.h"
#undef inputHostNativeVisibilityHistoryPending
#undef inputHostNativeVisibilityColdReturnLocked
#undef nativeVisibilityReturn
#undef probeRecordedOwner
#endif

static int checks=0,cases=0;
static void check(bool v,const char* name){++checks;if(!v){std::cout<<"FAIL "<<name<<"\n";throw std::runtime_error(name);}std::cout<<"PASS "<<name<<"\n";}
static std::string bytes(const fspath::path& p){std::ifstream f(p,std::ios::binary);return std::string(std::istreambuf_iterator<char>(f),{});}
static nw::LoadResult disk(){return nw::load(recordPath,inputHostTargetReturnMachineBinding());}
static void reset(const fspath::path& base,const char* name){
 ++cases;auto d=base/name;fspath::create_directories(d);recordPath=(d/"writer.json").wstring();logDirectory=d.wstring();
 rootId="USB\\VID_045E&PID_028E\\7&ABC&0&1";locations={"PCIROOT(0)#USBROOT(0)#USB(1)"};hidden.clear();events.clear();
 snapshotCalls=unhideCalls=ancestryCalls=absenceCalls=0;failUnhideAt=failSnapshotAt=0;onSnapshot={};onUnhide={};absence=ancestry=writersQuiet=true;
 g_inputHostVirtualIntentRevision=1;g_externalPriorityVirtualSuspended=false;g_shortcutRecordingActive=false;requestId=1;configuredPersona="disabled";powerGeneration=1;g_powerLifecycle=PowerLifecycle::Ready;g_inputHostProcess=nullptr;profile=ymcc::nativeForeground::Profile::Gpd;
 ymcc::inputHost::nativeVisibilityReturn::legacyExamined=false;ymcc::inputHost::nativeVisibilityReturn::legacyRejected=false;ymcc::inputHost::nativeVisibilityReturn::legacyCandidate.reset();
 std::cout<<"CASE "<<name<<"\n";
}
static nw::Visibility seed(bool issued,bool alive=false){
 nw::Visibility v;v.transactionId="private-visibility-1";v.parent={GetCurrentProcessId(),"1"};if(alive)v.parent=*ymcc::targetReturn::process::capture(GetCurrentProcess());
 v.physicalRoot=rootId;v.locationPaths=locations;v.paths={{"HID\\VID_045E&PID_028E&IG_01\\9&DEF&0&0000",nw::VisibilityPathPhase::Reserved},{rootId,nw::VisibilityPathPhase::Reserved}};
 {auto s=nw::Session::open(recordPath,inputHostTargetReturnMachineBinding());if(!s.ready()||!s.reserveVisibility(s.record()->revision,v))throw std::runtime_error("seed-reserve");if(issued)for(auto& p:v.paths){if(!s.markVisibilityMayHaveIssued(s.record()->revision,v.transactionId,p.instanceId))throw std::runtime_error("seed-issued");p.phase=nw::VisibilityPathPhase::MayHaveIssued;}}
 for(const auto& p:v.paths)hidden.push_back(U2W(p.instanceId));return v;
}
static void noWrites(const std::string& before,const char* label){check(unhideCalls==0&&bytes(recordPath)==before,label);}

static bool callCold(bool baseline,const char* reason){return baseline?baselineInputHostNativeVisibilityColdReturnLocked(reason):inputHostNativeVisibilityColdReturnLocked(reason);}
static bool isPending(bool baseline){return baseline?baselineInputHostNativeVisibilityHistoryPending():inputHostNativeVisibilityHistoryPending();}
static void clearPending(){ymcc::inputHost::nativeVisibilityReturn::pending=false;ymcc::inputHost::baselineNativeVisibilityReturn::pending=false;}
static void emptySchema2(){nw::Record r;r.schemaVersion=nw::kVisibilitySchemaVersion;r.machineBinding=inputHostTargetReturnMachineBinding();if(!nw::store(recordPath,r).committed())throw std::runtime_error("empty-schema2-store");}

// Explicit directed family boundary only; default8 cases remain below intact.
static int familyScope(const fspath::path& work,bool negative){
 struct Family {const char* name;const char* vendor;const char* model;ymcc::nativeForeground::Profile expected;};
 using P=ymcc::nativeForeground::Profile;
 const Family families[]={
  {"msi","MICRO-STAR INTERNATIONAL CO., LTD.","MS-1T91",P::Msi},
  {"rog","ASUSTEK COMPUTER INC.","ROG Ally X RC73XA",P::Rog},
  {"onex","ONE-NETBOOK TECHNOLOGY CO., LTD.","ONEXPLAYER 3",P::Unsupported},
  {"aya","AYANEO","AYANEO 3",P::Unsupported},
  {"lenovo","LENOVO","83E1",P::Unsupported},
  {"aokzoe","AOKZOE","A1 Pro",P::Unsupported},
  {"pc","ASUSTEK COMPUTER INC.","ROG CROSSHAIR X870E HERO",P::Unsupported}
 };
 for(const auto& family:families)for(const auto* shape:{"live-gpd-history","dead-gpd-history","corrupt-history","locked-gpd-history"}){
  const std::string name=std::string(family.name)+"/"+shape;
  reset(work,name.c_str());clearPending();
  if(std::string(shape)=="corrupt-history"){
   std::ofstream out(recordPath,std::ios::binary);out<<"{invalid-gpd-history";out.close();
  }else seed(true,std::string(shape)!="dead-gpd-history");
  // Unrelated hidden entries must survive even an accidentally copied ledger.
  hidden.push_back(L"HID\\VID_F00D&PID_0001\\FOREIGN_UNRELATED");
  const auto original=bytes(recordPath);const auto priorHidden=hidden;
  HANDLE lock=INVALID_HANDLE_VALUE;
  if(std::string(shape)=="locked-gpd-history"){
   const auto owner=recordPath+L".owner";
   lock=CreateFileW(owner.c_str(),GENERIC_READ|GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
   check(lock!=INVALID_HANDLE_VALUE,"exact-owned-record-lock-held");
  }
  profile=ymcc::nativeForeground::machineProfile(family.vendor,family.model);
  check(profile==family.expected,"actual-family-profile-matches-declared-route");
  check(callCold(negative,name.c_str())&&!isPending(negative),"non-gpd-old-history-cannot-block-or-create-gpd-debt");
  check(snapshotCalls==0&&unhideCalls==0&&ancestryCalls==0&&absenceCalls==0&&hidden==priorHidden&&bytes(recordPath)==original,
    "non-gpd-old-history-does-not-read-device-or-change-hidden-or-ledger");
  if(lock!=INVALID_HANDLE_VALUE)check(CloseHandle(lock)!=FALSE,"exact-owned-record-lock-returned");
 }
 reset(work,"gpd/control-live-owner");clearPending();seed(true,true);
 const auto liveBefore=bytes(recordPath);
 check(!callCold(false,"gpd/control-live-owner")&&isPending(false),"gpd-live-owned-history-still-blocks-unsafe-unhide");
 noWrites(liveBefore,"gpd-live-owned-history-and-hidden-responsibility-retained");
 reset(work,"gpd/control-dead-owner");clearPending();seed(true);
 check(callCold(false,"gpd/control-dead-owner")&&!isPending(false),"gpd-dead-owner-exact-return-still-completes");
 check(hidden.empty()&&!disk().record->nativeVisibility&&unhideCalls==2,"gpd-positive-uses-actual-store-and-readback");
 std::cout<<"RESULT familyScopeCases="<<cases<<" checks="<<checks<<" failed=0\n";
 return 0;
}


#ifdef YMCC_NATIVE_VISIBILITY_EMPTY_TAIL_TEST
static const std::vector<std::string> emptyTailCases={
 "empty-tail-off-no-location","empty-tail-steamdeck-changed-location",
 "empty-tail-live-parent","empty-tail-unknown-parent","empty-tail-writers-busy",
 "empty-tail-owner-lock-held","empty-tail-machine-mismatch",
 "empty-tail-probe-power-change","empty-tail-probe-intent-change",
 "issued-tail-changed-location","issued-tail-same-location","family-msi-excluded"};
static bool selectEmptyTail(const std::string& selection,std::vector<std::string>& selected,int& version){
 version=0;
 if(selection=="empty-tail"){selected=emptyTailCases;return true;}
 if(selection=="baseline-empty-tail"){version=1;selected={emptyTailCases.front()};return true;}
 if(selection=="mutation-current-empty-tail"){version=2;selected={"empty-tail-probe-power-change"};return true;}
 const std::string prefix="empty-tail:";
 if(selection.rfind(prefix,0)!=0)return false;
 const auto name=selection.substr(prefix.size());
 if(std::find(emptyTailCases.begin(),emptyTailCases.end(),name)==emptyTailCases.end())return false;
 selected={name};return true;
}
static bool emptyTailCall(int version,const char* reason){
 return version==1?beforeEmptyTailColdReturn(reason):version==2?currentRemovedEmptyTailColdReturn(reason):inputHostNativeVisibilityColdReturnLocked(reason);
}
static nw::Visibility seedEmptyTail(bool alive){
 auto v=seed(false,alive);
 {auto s=nw::Session::open(recordPath,inputHostTargetReturnMachineBinding());
  if(!s.ready())throw std::runtime_error("empty-tail-seed-session");
  for(const auto& path:v.paths)if(!s.settleVisibilityPath(s.record()->revision,v.transactionId,path.instanceId))throw std::runtime_error("empty-tail-seed-settle");}
 v.paths.clear();return v;
}
static int runEmptyTail(const fspath::path& work,const std::vector<std::string>& selected,int version){
 using namespace ymcc::targetReturn::process;
 const std::wstring foreign=L"HID\\VID_9999&PID_9999\\FOREIGN";
 for(const auto& name:selected){
  reset(work,name.c_str());clearPending();
  ymcc::inputHost::beforeEmptyTailReturn::pending=false;
  ymcc::inputHost::currentRemovedEmptyTailReturn::pending=false;
  coldProbeUnknown=false;coldProbeCalls=0;coldProbeAction={};
  if(name=="family-msi-excluded"){
   emptySchema2();const auto original=bytes(recordPath);profile=ymcc::nativeForeground::machineProfile("MICRO-STAR","MS-1T91");
   check(emptyTailCall(version,name.c_str())&&!isPending(false),"other-family-has-no-gpd-cold-debt");
   check(snapshotCalls==0&&unhideCalls==0&&ancestryCalls==0&&absenceCalls==0&&bytes(recordPath)==original,"other-family-keeps-original-visibility-authority");continue;
  }
  const bool issued=name.rfind("issued-tail-",0)==0;
  if(issued)seed(true);else seedEmptyTail(name=="empty-tail-live-parent");
  if(issued)hidden.push_back(foreign);else hidden={foreign};
  if(name=="empty-tail-steamdeck-changed-location")configuredPersona="steamdeck";
  if(name=="empty-tail-off-no-location")locations.clear();
  if(name=="empty-tail-steamdeck-changed-location"||name=="issued-tail-changed-location")locations={"PCIROOT(0)#USBROOT(0)#USB(9)"};
  if(name=="empty-tail-unknown-parent")coldProbeUnknown=true;
  if(name=="empty-tail-writers-busy")writersQuiet=false;
  if(name=="empty-tail-probe-power-change")coldProbeAction=[](){++powerGeneration;};
  if(name=="empty-tail-probe-intent-change")coldProbeAction=[](){g_inputHostVirtualIntentRevision.fetch_add(1);};
  if(name=="empty-tail-machine-mismatch"){
   auto r=*disk().record;r.machineBinding="windows-machine-guid:other-private-machine";
   if(!nw::store(recordPath,r).committed())throw std::runtime_error("machine-mismatch-seed");
  }
  const auto original=bytes(recordPath);const auto originalHidden=hidden;
  const bool success=name=="empty-tail-off-no-location"||name=="empty-tail-steamdeck-changed-location"||name=="issued-tail-same-location";
  bool returned=false;
  if(name=="empty-tail-owner-lock-held"){
   auto held=nw::Session::open(recordPath,inputHostTargetReturnMachineBinding());check(held.ready(),"real-owner-session-held");
   returned=emptyTailCall(version,name.c_str());
  }else returned=emptyTailCall(version,name.c_str());
  check(returned==success,success?"current-empty-or-issued-tail-must-complete":"unsafe-empty-or-issued-tail-must-refuse");
  if(success){
   const auto result=disk();check(result.state==nw::LoadState::Valid&&result.record&&!result.record->nativeVisibility,"real-store-metadata-cleared");
   check(hidden==std::vector<std::wstring>{foreign},"foreign-hidden-entry-preserved");
   if(!issued)check(snapshotCalls==0&&unhideCalls==0&&ancestryCalls==0&&absenceCalls==0,"empty-tail-zero-snapshot-unhide-ancestry-absence");
   else check(snapshotCalls==4&&unhideCalls==2&&ancestryCalls==4&&absenceCalls==1,"issued-same-location-original-return-path");
  }else{
   check(bytes(recordPath)==original,"rejected-tail-disk-bytes-unchanged");
   check(hidden==originalHidden,"rejected-tail-hidden-entries-unchanged");
   check(snapshotCalls==0&&unhideCalls==0&&ancestryCalls==0&&absenceCalls==0,"rejected-tail-zero-device-effects");
  }
  if(name=="empty-tail-unknown-parent"||name=="empty-tail-probe-power-change"||name=="empty-tail-probe-intent-change")check(coldProbeCalls==1,"actual-owner-probe-wrapper-reached-once");
 }
 std::cout<<"RESULT selectedCases="<<cases<<" checks="<<checks<<" failed=0\n";return 0;
}
#endif

int main(int argc,char** argv){try{
 if(argc!=3)return 20;
 if(std::string(argv[1])=="candidate-family-scope"||std::string(argv[1])=="baseline-family-scope"){
  const fspath::path work=argv[2];if(!work.is_absolute())return 21;
  return familyScope(work,std::string(argv[1])=="baseline-family-scope");
 }
#ifdef YMCC_NATIVE_VISIBILITY_EMPTY_TAIL_TEST
 if(std::string(argv[1])!="candidate-selected"&&std::string(argv[1])!="baseline-hostlive"){
  std::vector<std::string> selected;int version=0;
  if(!selectEmptyTail(argv[1],selected,version))return 20;
  const fspath::path selectedWork=argv[2];if(!selectedWork.is_absolute()||selectedWork.wstring().size()>120)return 21;
  return runEmptyTail(selectedWork,selected,version);
 }
#endif
 const bool baseline=std::string(argv[1])=="baseline-hostlive";
 if(!baseline&&std::string(argv[1])!="candidate-selected")return 20;
 const fspath::path work=argv[2];if(!work.is_absolute()||work.wstring().size()>120)return 21;
 reset(work,"empty-schema2-host-live");clearPending();emptySchema2();const auto before=bytes(recordPath);
 configuredPersona="steamdeck";g_inputHostProcess=reinterpret_cast<HANDLE>(uintptr_t(1));
 check(!callCold(baseline,"empty-schema2-host-live"),"host-live-current-rejected");
 // The same assertion must fail when the historical pending assignment is restored.
 check(!isPending(baseline),"empty-schema2-host-live-must-not-create-pending");
 check(unhideCalls==0&&snapshotCalls==0&&bytes(recordPath)==before,"host-live-no-device-snapshot-or-ledger-write");
 if(baseline)return 24; // Reaching this line means the historical counterexample failed to detect regression.

 reset(work,"durable-parent-alive");clearPending();seed(true,true);const auto aliveBefore=bytes(recordPath);
 check(!callCold(false,"durable-parent-alive")&&isPending(false),"actual-durable-parent-alive-establishes-pending");
 check(unhideCalls==0&&bytes(recordPath)==aliveBefore,"parent-alive-retains-real-disk-debt");

 reset(work,"durable-unhide-fail");clearPending();seed(true);failUnhideAt=1;
 check(!callCold(false,"durable-unhide-fail")&&isPending(false),"actual-durable-unhide-fail-establishes-pending");
 check(disk().record->nativeVisibility->paths.size()==2&&unhideCalls==1,"failed-return-retains-full-durable-path-set");

 reset(work,"durable-returned");clearPending();seed(true);ymcc::inputHost::nativeVisibilityReturn::pending=true;
 onUnhide=[](int){check(inputHostNativeVisibilityHistoryPending(),"pending-retained-during-unhide-until-readback-and-persist");};
 check(callCold(false,"durable-returned")&&!isPending(false),"actual-readback-and-persisted-return-clears-pending");
 check(hidden.empty()&&!disk().record->nativeVisibility,"real-disk-complete-and-configuration-returned");
 struct Family {const char* name;const char* vendor;const char* model;};
 for(const auto& family:std::vector<Family>{{"family-msi-excluded","MICRO-STAR","MS-1T91"},
     {"family-rog-excluded","ASUSTEK","RC73XA"},{"family-onex-excluded","ONE-NETBOOK","ONEXPLAYER 3"},
     {"family-aya-excluded","AYANEO","AYANEO 2"}}){
  reset(work,family.name);clearPending();emptySchema2();const auto original=bytes(recordPath);
  profile=ymcc::nativeForeground::machineProfile(family.vendor,family.model);
  check(callCold(false,family.name)&&!isPending(false),"other-family-has-no-gpd-cold-debt");
  check(snapshotCalls==0&&unhideCalls==0&&ancestryCalls==0&&absenceCalls==0&&bytes(recordPath)==original,
      "other-family-keeps-original-visibility-authority");
 }
 std::cout<<"RESULT selectedCases="<<cases<<" checks="<<checks<<" failed=0\n";return 0;
}catch(const std::exception& e){std::cerr<<"EXCEPTION "<<e.what()<<"\n";return 23;}}
