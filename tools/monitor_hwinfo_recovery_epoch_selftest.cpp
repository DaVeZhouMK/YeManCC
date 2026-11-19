#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <future>
#include "sleep_power_state.h"
#include <functional>
#include <vector>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "monitor_hwinfo_manager.h"
#include "monitor_hwinfo_recovery.h"
using json=nlohmann::json;
enum class PowerLifecycle{Ready,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<bool> g_poolCancel{false};
static unsigned long long powerGeneration=1,tick=100000;static unsigned managedCalls=0;
static bool demandActive=true,queueAvailable=true,lastElevationAllowed=true;
static std::vector<std::function<void()>> jobs;static json events=json::array();
static unsigned long long currentPowerGeneration(){return powerGeneration;}
static std::atomic<bool> sharedKnown{true},sharedGate{true},sharedPending{false};
static std::atomic<unsigned long long> sharedSequence{1},sharedSerial{0},sharedRevision{1},sharedReads{0};
static int __cdecl fixtureSharedRead(ymcc::sleeppower::SharedPowerV1* out,uint32_t bytes){
 ++sharedReads;if(!sharedKnown||bytes!=sizeof(*out))return 0;
 *out={};out->generation=powerGeneration;out->publicationRevision=sharedRevision;out->observedNotificationSequence=sharedSequence;out->suspendSerial=sharedSerial;
 out->phase=g_powerLifecycle==PowerLifecycle::Ready?0:2;out->flags=ymcc::sleeppower::kKnown|(sharedGate?ymcc::sleeppower::kHardwareAllowed:0)|(sharedPending?ymcc::sleeppower::kNotificationPending:0);return 1;
}
static bool captureSharedPowerLifecycle(ymcc::sleeppower::SharedPowerV1& out){return ymcc::sleeppower::capturePower(&fixtureSharedRead,out);}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& p){return ymcc::sleeppower::powerCurrent(&fixtureSharedRead,p);}
// ACTUAL_SHARED_READY
static bool monitorHWiNFODemandActive(){return demandActive;}
static void appendNativeLifecycleLog(const char*name,const json& detail){events.push_back({{"event",name},{"detail",detail}});}
// ACTUAL_GLOBALS
static ymcc::hwinfo::Result managedResult;static int managementThrows=0;
static ymcc::hwinfo::Result nativeHWiNFOManage(ymcc::hwinfo::Request request,unsigned long long generation=0,unsigned long long demand=0,bool permit=true,const ymcc::sleeppower::SharedPowerV1* expectedPower=nullptr){++managedCalls;lastElevationAllowed=permit;if(managementThrows==2)throw 42;if(managementThrows==1)throw std::runtime_error("inert manager exception");if(!request.restart||!request.recoverOnly||!generation)throw std::runtime_error("wrong management request");return managedResult;}
static bool poolSubmit(std::function<void()> job){if(!queueAvailable)return false;jobs.push_back(std::move(job));return true;}
static bool monitorHWiNFOCancelled();
// ACTUAL_WORKER
// ACTUAL_CANCELLATION
static bool g_nativeHWiNFORecoveryArmed=false,g_hwInfoRecoveryCancelLogged=false;
#define GetTickCount64() tick
static bool admission(){return
// ACTUAL_ADMISSION
;}
static void consume(ymcc::monitorrecovery::RetryBudget& hwRecoveryBudget,ymcc::monitorrecovery::StalledProducer& hwProducerStall,bool hwHealthy=false){
ymcc::sleeppower::SharedPowerV1 collectionPower;captureSharedPowerLifecycle(collectionPower);
// ACTUAL_CONSUMER
}
#undef GetTickCount64
static void publish(int code,unsigned long long power,unsigned long long demand=1){g_monitorHWiNFORecoveryGeneration=power;g_monitorHWiNFORecoveryDemandGeneration=demand;g_monitorHWiNFORecoveryResult=code;
#if HW_SHARED_POWER
 g_monitorHWiNFORecoveryNotification=sharedSequence.load();g_monitorHWiNFORecoverySuspendSerial=sharedSerial.load();
#endif
}
static std::mutex g_nativeMonitorMx;static std::condition_variable g_nativeMonitorCv;static bool g_nativeMonitorStop=false;
// ACTUAL_POWER_WAIT
namespace ymcc::hwinfo {
static unsigned fixtureManagerCalls=0;static std::function<void()> fixtureManagerStep;static std::function<bool()> fixtureCancellation;
inline Result fixtureManagerRun(Request,std::function<bool()> cancelled,bool){++fixtureManagerCalls;fixtureCancellation=cancelled;if(fixtureManagerStep)fixtureManagerStep();return cancelled&&cancelled()?Result{15,false,ERROR_CANCELLED,"fixture cancelled",{}}:Result{};}
}
namespace actualManagement {
static std::wstring POWER_CONTROL_DIR=L"C:/inert/PowerControl";
#define run fixtureManagerRun
// ACTUAL_MANAGER
#undef run
}
static int runSharedPower(const char* output){json checks=json::array();bool ok=true;const auto check=[&](const char*name,bool good){checks.push_back({{"name",name},{"ok",good}});ok&=good;};
 const auto reset=[&]{g_powerLifecycle=PowerLifecycle::Ready;powerGeneration=7;sharedKnown=true;sharedGate=true;sharedPending=false;sharedSequence=31;sharedSerial=4;sharedRevision=20;g_poolCancel=false;demandActive=true;queueAvailable=true;g_monitorHWiNFOManagementPending=false;g_monitorHWiNFORecoveryResult=-1;g_monitorHWiNFODemandGeneration=1;jobs.clear();};
 const auto execute=[&]{if(!jobs.empty()){auto job=std::move(jobs.front());jobs.erase(jobs.begin());job();}};
 reset();sharedPending=true;check("pending notification blocks recovery before phase projection",monitorHWiNFOCancelled()&&!monitorLaunchHWiNFORecovery()&&jobs.empty());
 reset();sharedGate=false;check("same Ready phase with closed shared gate blocks recovery",monitorHWiNFOCancelled()&&!monitorLaunchHWiNFORecovery());
 reset();sharedKnown=false;check("unknown shared reader never grants recovery",monitorHWiNFOCancelled()&&!monitorLaunchHWiNFORecovery());
 reset();auto before=managedCalls;monitorLaunchHWiNFORecovery();++sharedSequence;execute();check("same-generation notification retires the queued worker",managedCalls==before&&g_monitorHWiNFORecoveryResult==15&&!g_monitorHWiNFOManagementPending);
 reset();before=managedCalls;monitorLaunchHWiNFORecovery();++sharedSerial;execute();check("same-generation suspend serial retires the queued worker",managedCalls==before&&g_monitorHWiNFORecoveryResult==15);
 reset();before=managedCalls;monitorLaunchHWiNFORecovery();++sharedRevision;managedResult={};execute();check("diagnostic publication advance preserves healthy queued work",managedCalls==before+1&&g_monitorHWiNFORecoveryResult==0);
 reset();before=managedCalls;monitorLaunchHWiNFORecovery();++g_monitorHWiNFODemandGeneration;execute();check("OFF ON demand still retires old queued work",managedCalls==before&&g_monitorHWiNFORecoveryResult==15);
 reset();ymcc::monitorrecovery::RetryBudget b;ymcc::monitorrecovery::StalledProducer stall;consume(b,stall);b.started();publish(7,7);++sharedSequence;consume(b,stall);check("old same-generation completion cannot impose a new cooldown",!b.waiting);
 reset();consume(b,stall);publish(7,7);consume(b,stall);check("current failure preserves the original thirty-second cooldown",b.waiting&&b.retryAt==tick+30000);
 reset();ymcc::hwinfo::fixtureManagerCalls=0;ymcc::hwinfo::fixtureManagerStep=[]{sharedPending=true;++sharedSequence;};auto res=actualManagement::nativeHWiNFOManage({});const auto cancelled=ymcc::hwinfo::fixtureCancellation;sharedPending=false;check("actual manager cannot revive its cancellation after same-generation Ready",res.code==15&&cancelled&&cancelled());
 reset();ymcc::hwinfo::fixtureManagerStep=[]{++sharedRevision;};res=actualManagement::nativeHWiNFOManage({});check("actual manager tolerates metadata-only publication",res.code==0);
 reset();sharedKnown=false;const auto managerBefore=ymcc::hwinfo::fixtureManagerCalls;res=actualManagement::nativeHWiNFOManage({});check("actual manager rejects unknown before producer operations",res.code==15&&ymcc::hwinfo::fixtureManagerCalls==managerBefore);
#if HW_SHARED_POWER
 reset();sharedGate=false;sharedReads=0;std::promise<bool> done;auto future=done.get_future();std::thread waiter([&]{ymcc::sleeppower::SharedPowerV1 p;done.set_value(monitorWaitForSharedPower(p));});
 const auto limit=GetTickCount64()+2000;while(!sharedReads&&GetTickCount64()<limit)Sleep(1);const auto reads=sharedReads.load();const bool blocked=future.wait_for(std::chrono::milliseconds(40))==std::future_status::timeout;check("collector shared wait performs no periodic power query",blocked&&reads==sharedReads.load());
 {std::lock_guard<std::mutex> lock(g_nativeMonitorMx);sharedGate=true;g_nativeMonitorCv.notify_all();}const bool ready=future.wait_for(std::chrono::seconds(2))==std::future_status::ready;if(!ready){std::lock_guard<std::mutex> lock(g_nativeMonitorMx);g_nativeMonitorStop=true;g_nativeMonitorCv.notify_all();}waiter.join();check("shared owner notification resumes the same collector wait",ready&&future.get());
 reset();sharedGate=false;sharedReads=0;std::promise<bool> stopped;auto stopFuture=stopped.get_future();std::thread finalWaiter([&]{ymcc::sleeppower::SharedPowerV1 p;stopped.set_value(monitorWaitForSharedPower(p));});const auto stopLimit=GetTickCount64()+2000;while(!sharedReads&&GetTickCount64()<stopLimit)Sleep(1);{std::lock_guard<std::mutex> lock(g_nativeMonitorMx);g_nativeMonitorStop=true;g_nativeMonitorCv.notify_all();}finalWaiter.join();check("application stop returns the suspended collector without Ready",!stopFuture.get());g_nativeMonitorStop=false;
#else
 check("collector shared wait performs no periodic power query",false);check("shared owner notification resumes the same collector wait",false);check("application stop returns the suspended collector without Ready",false);
#endif
 const json result={{"ok",ok},{"selection","shared-power"},{"cases",checks},{"systemPowerQueries",0},{"hardwareWrites",false},{"error",""}};const auto text=result.dump(2);const auto f=CreateFileW(std::filesystem::path(output).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)return 2;DWORD wrote=0;const bool saved=WriteFile(f,text.data(),(DWORD)text.size(),&wrote,nullptr)&&wrote==text.size();CloseHandle(f);std::cout<<text;return saved?(ok?0:3):2;
}
static int runScopeRearm(const char* output){
 json checks=json::array();bool ok=true;const auto check=[&](const char*name,bool good){checks.push_back({{"name",name},{"ok",good}});ok&=good;};
 for(unsigned mode=0;mode<3;++mode){
  ymcc::sleeppower::SharedPowerV1 previousCollectionPower;captureSharedPowerLifecycle(previousCollectionPower);auto collectionPower=previousCollectionPower;
  if(mode==2)++collectionPower.publicationRevision;else ++collectionPower.observedNotificationSequence;
  ymcc::monitorrecovery::RetryBudget hwRecoveryBudget;hwRecoveryBudget.waiting=mode==1;hwRecoveryBudget.retryAt=123456;
  bool g_nativeHWiNFORecoveryArmed=false;struct Reset {unsigned count=0;void reset(){++count;}void stop(){++count;}}hwParseReuse,hwProducerStall,frameOutput,batteryModel,batteryFallback,fpsLimitReader,fileSystemReader;
  unsigned g_monitorFreshness=9;long long g_monitorLastFreshMs=42;
// ACTUAL_COLLECTION_SCOPE
  if(mode==0)check("same-generation Ready rearms cancelled recovery",g_nativeHWiNFORecoveryArmed&&!hwRecoveryBudget.waiting&&hwParseReuse.count==1);
  if(mode==1)check("owed failure cooldown survives a new notification scope",!g_nativeHWiNFORecoveryArmed&&hwRecoveryBudget.waiting&&hwRecoveryBudget.retryAt==123456);
  if(mode==2)check("metadata-only publication does not rearm or reset",!g_nativeHWiNFORecoveryArmed&&hwParseReuse.count==0&&g_monitorFreshness==9&&g_monitorLastFreshMs==42);
 }
 const auto text=json{{"selection","scope-rearm"},{"ok",ok},{"cases",checks},{"error",""},{"hardwareWrites",false}}.dump(2);std::ofstream(output)<<text;std::cout<<text;return ok?0:3;
}
struct TransactionOps {
 std::vector<std::string> calls;std::string invalidate;bool cancelled=false,healthyValue=false;unsigned proofCount=0,released=0;
 void call(const char*n){calls.push_back(n);if(invalidate==n)cancelled=true;}
 bool interrupted(){return cancelled;}bool acquire(unsigned){call("acquire");return true;}void release(){++released;}
 bool healthy(){call("healthy");return healthyValue;}void applyAffinity(){call("affinity");}
 std::wstring resolve(const std::wstring&){call("resolve");return L"C:\\inert\\HWiNFO64.exe";}
 void prepare(const std::wstring&,const ymcc::hwinfo::Request&){call("prepare");}void stop(){call("stop");}
 void configure(const std::wstring&,const ymcc::hwinfo::Request&){call("configure");}
 std::string sensorBudget(const std::wstring&){call("sensorBudget");return "inert";}void assertStopped(){call("assertStopped");}
 void launch(const std::wstring&){call("launch");}bool exited(){call("exited");return false;}
 std::optional<ymcc::hwinfo::Proof> proof(unsigned){++proofCount;if(proofCount==2)call("second-proof");ymcc::hwinfo::Proof p;p.poll=proofCount;return p;}
 bool wait(unsigned){call("wait");return !cancelled;}
 bool saw(const char*n){return std::find(calls.begin(),calls.end(),n)!=calls.end();}
};
namespace launchTest {
using namespace ymcc::hwinfo;
static DWORD shellError=0;static bool hidden=false,handleRequested=false,silent=false,affinityApplied=false;static std::wstring path,directory,verb;
static BOOL shell(SHELLEXECUTEINFOW* info){path=info->lpFile;directory=info->lpDirectory;verb=info->lpVerb;hidden=info->nShow==SW_HIDE;handleRequested=(info->fMask&SEE_MASK_NOCLOSEPROCESS)!=0;silent=(info->fMask&SEE_MASK_FLAG_NO_UI)!=0;if(shellError){SetLastError(shellError);return FALSE;}info->hProcess=CreateEventW(nullptr,TRUE,FALSE,nullptr);return info->hProcess!=nullptr;}
static BOOL affinity(HANDLE,DWORD_PTR){affinityApplied=true;return TRUE;}
struct Operations {
 Handle producer;void assertStopped(){}
#define ShellExecuteExW shell
#define SetProcessAffinityMask affinity
// ACTUAL_LAUNCH
#undef SetProcessAffinityMask
#undef ShellExecuteExW
};
}
namespace elevationTest {
using namespace ymcc::hwinfo;
static unsigned shellCalls=0;static bool cancelBeforeElevation=false,cancelFromTransaction=false,shellReturnsChild=false,parentProofAvailable=false;
struct Operations:TransactionOps {
 std::function<bool()> callback;
 Operations(std::wstring,std::function<bool()> cb):callback(cb){}
 bool interrupted(){return callback&&callback();}
 bool acquire(unsigned){if(cancelBeforeElevation)cancelFromTransaction=true;throw Error(5,"inert access denied",true,5);}
 std::optional<Proof> proof(unsigned period){return parentProofAvailable?TransactionOps::proof(period):std::optional<Proof>{};}
};
static BOOL inertShell(SHELLEXECUTEINFOW* info){++shellCalls;if(shellReturnsChild){info->hProcess=CreateEventW(nullptr,TRUE,TRUE,nullptr);return info->hProcess!=nullptr;}SetLastError(ERROR_CANCELLED);return FALSE;}
static DWORD childResultCode=0;
static BOOL inertChildExit(HANDLE,LPDWORD code){*code=childResultCode;return TRUE;}
#define GetExitCodeProcess inertChildExit
#define ShellExecuteExW inertShell
// ACTUAL_RUN
#undef ShellExecuteExW
#undef GetExitCodeProcess
static Result invoke(bool permit){
#if HW_RUN_HAS_PERMISSION
 return actualRun({},[]{return cancelFromTransaction;},permit);
#else
 return actualRun({},[]{return cancelFromTransaction;});
#endif
}
}
// HWINFO_STARVATION_RECOVERY_SELECTION_BEGIN
static constexpr const char* kStarvationRecoveryCaseNames[] = {
 "producer-recovers-during-resolve",
 "producer-recovers-during-prepare",
 "producer-healthy-before-transaction",
 "persistent-stall-still-recovers-and-proves-two-samples",
 "explicit-user-restart-remains-explicit",
 "cancellation-before-stop-still-wins"
};
struct StarvationRecoveryOps:TransactionOps {
 std::string recoverAt;bool denyRecoveredStop=false;unsigned healthReads=0,stopCalls=0;
 bool healthy(){++healthReads;return TransactionOps::healthy();}
 std::wstring resolve(const std::wstring& path){auto result=TransactionOps::resolve(path);if(recoverAt=="resolve")healthyValue=true;return result;}
 void prepare(const std::wstring& path,const ymcc::hwinfo::Request& request){TransactionOps::prepare(path,request);if(recoverAt=="prepare")healthyValue=true;}
 void stop(){++stopCalls;if(denyRecoveredStop&&healthyValue)throw ymcc::hwinfo::Error(4,"inert stop refusal: already healthy producer");TransactionOps::stop();}
};
static int runStarvationRecovery(const char* output){json rows=json::array();unsigned failed=0;
 const auto scenario=[&](const char* name,StarvationRecoveryOps ops,ymcc::hwinfo::Request request,bool mustReuse,int expectedCode){
  const auto result=ymcc::hwinfo::transaction(ops,request);
  const bool ok=result.code==expectedCode&&ops.released==1&&(mustReuse?ops.stopCalls==0&&!ops.saw("launch")&&result.dataReady:expectedCode==15?ops.stopCalls==0&&!ops.saw("launch"):ops.stopCalls==1&&ops.saw("launch")&&ops.proofCount==2);
  if(!ok)++failed;rows.push_back({{"name",name},{"ok",ok},{"code",result.code},{"detail",result.detail},{"dataReady",result.dataReady},{"healthReads",ops.healthReads},{"stops",ops.stopCalls},{"launches",ops.saw("launch")?1:0},{"proofs",ops.proofCount},{"mutexReleased",ops.released},{"calls",ops.calls}});
 };
 ymcc::hwinfo::Request automatic;automatic.restart=true;automatic.recoverOnly=true;
 for(const char* stage:{"resolve","prepare"}){StarvationRecoveryOps ops;ops.recoverAt=stage;ops.denyRecoveredStop=true;scenario(stage==std::string("resolve")?kStarvationRecoveryCaseNames[0]:kStarvationRecoveryCaseNames[1],ops,automatic,true,0);}
 {StarvationRecoveryOps ops;ops.healthyValue=true;scenario(kStarvationRecoveryCaseNames[2],ops,automatic,true,0);}
 {StarvationRecoveryOps ops;scenario(kStarvationRecoveryCaseNames[3],ops,automatic,false,0);}
 {StarvationRecoveryOps ops;ops.healthyValue=true;ymcc::hwinfo::Request explicitRestart;explicitRestart.restart=true;scenario(kStarvationRecoveryCaseNames[4],ops,explicitRestart,false,0);}
 {StarvationRecoveryOps ops;ops.recoverAt="prepare";ops.invalidate="prepare";scenario(kStarvationRecoveryCaseNames[5],ops,automatic,false,15);}
 const json report={{"selection","starvation-recovery"},{"ok",failed==0},{"failed",failed},{"cases",rows},{"consumer","original ymcc::hwinfo::transaction template and existing TransactionOps with inert OS facts"},{"hardwareOrProcessCalls",0},{"realCpuPressure",false},{"existingStallGraceMs",ymcc::monitorrecovery::StalledProducer::graceMs},{"retryCooldownMs",ymcc::monitorrecovery::RetryBudget::retryDelayMs}};
 const auto text=report.dump(2);const auto target=std::filesystem::path(output);
 const auto file=CreateFileW(target.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
 if(file==INVALID_HANDLE_VALUE){std::cerr<<"Refuse missing or existing original result";return 2;}
 DWORD wrote=0;const bool saved=WriteFile(file,text.data(),static_cast<DWORD>(text.size()),&wrote,nullptr)&&wrote==text.size();CloseHandle(file);
 if(!saved)return 2;std::cout<<text;return failed?1:0;
}
// HWINFO_STARVATION_RECOVERY_SELECTION_END
int main(int argc,char**argv){if(argc==3&&std::string(argv[2])=="--scope-rearm")return runScopeRearm(argv[1]);if(argc==3&&std::string(argv[2])=="--shared-power")return runSharedPower(argv[1]);if(argc==3&&std::string(argv[2])=="--starvation-recovery")return runStarvationRecovery(argv[1]);if(argc!=2)return 2;bool ok=true;std::string error;json checks=json::array();
const auto check=[&](const char*n,bool pass){checks.push_back({{"name",n},{"ok",pass}});ok&=pass;};
try{using ymcc::monitorrecovery::RetryBudget;using ymcc::monitorrecovery::StalledProducer;
 RetryBudget budget;StalledProducer stall;consume(budget,stall);bool slow=true,keepsTrying=true;
 for(unsigned i=0;i<100;++i){budget.started();publish(i%2?11:7,1);consume(budget,stall);slow&=budget.waiting&&budget.retryAt==tick+30000;keepsTrying&=budget.waiting;tick+=30000;}
 check("one power generation keeps recovering beyond three failures",keepsTrying&&budget.attempts==100);
 check("every failure including a busy mutex waits at least thirty seconds",slow);
 g_nativeHWiNFORecoveryArmed=false;powerGeneration=2;g_powerLifecycle=PowerLifecycle::Suspended;consume(budget,stall);
 check("suspend cannot reset or arm a disconnected recovery",budget.attempts==100&&!g_nativeHWiNFORecoveryArmed);
 g_powerLifecycle=PowerLifecycle::Ready;consume(budget,stall);
 check("new Ready generation restores recovery admission",budget.attempts==0&&!budget.waiting&&g_nativeHWiNFORecoveryArmed);
 g_monitorHWiNFOManagementPending=true;check("old worker cannot consume the new recovery admission",!admission()&&g_nativeHWiNFORecoveryArmed);g_monitorHWiNFOManagementPending=false;
 check("new recovery stays armed until the old worker is released",admission());
 RetryBudget next;consume(next,stall);next.started();publish(15,1);consume(next,stall);
 check("old completion cannot delay the new generation",next.attempts==1&&!next.waiting);
 publish(0,1);consume(next,stall);check("old successful receipt cannot alter disconnected recovery",next.attempts==1&&!next.waiting);
 publish(7,2);consume(next,stall);check("current failure schedules the slow retry",next.waiting&&next.retryAt==tick+30000);
 g_nativeHWiNFORecoveryArmed=false;tick=next.retryAt-1;consume(next,stall);check("retry cannot run before its cooldown",!g_nativeHWiNFORecoveryArmed&&next.waiting);
 tick=next.retryAt;g_monitorHWiNFOManagementPending=true;consume(next,stall);check("pending native worker prevents duplicate retry admission",!g_nativeHWiNFORecoveryArmed&&next.waiting);
 g_monitorHWiNFOManagementPending=false;consume(next,stall);check("due retry is admitted after the owned worker exits",g_nativeHWiNFORecoveryArmed&&!next.waiting);
 g_powerLifecycle=PowerLifecycle::Suspended;check("non-ready power phase cancels HWiNFO management",monitorHWiNFOCancelled());g_powerLifecycle=PowerLifecycle::Ready;
 check("Ready phase permits the existing management path",!monitorHWiNFOCancelled());g_poolCancel=true;check("application shutdown still cancels management",monitorHWiNFOCancelled());g_poolCancel=false;
 const auto runJob=[&]{auto job=std::move(jobs.front());jobs.erase(jobs.begin());job();};
 managedResult={15,false,0,"cancelled old generation",{}};powerGeneration=2;g_monitorHWiNFORecoveryResult=-1;const auto before=managedCalls;
 check("automatic recovery queues exactly one owned worker",monitorLaunchHWiNFORecovery()&&jobs.size()==1&&managedCalls==before&&g_monitorHWiNFOManagementPending);
 check("busy admission reports no new owned transaction",!monitorLaunchHWiNFORecovery()&&jobs.size()==1);
 powerGeneration=3;runJob();check("late automatic worker publishes its captured power generation",g_monitorHWiNFORecoveryGeneration==2&&g_monitorHWiNFORecoveryResult==15&&!g_monitorHWiNFOManagementPending);
 check("obsolete power worker never calls the native transaction",managedCalls==before);
 RetryBudget afterWake;consume(afterWake,stall);check("actual late worker result cannot delay the new collector episode",afterWake.attempts==0&&!afterWake.waiting);
 managedResult={7,false,0,"inert failure",{}};monitorLaunchHWiNFORecovery();const auto beforeOff=managedCalls;
 demandActive=false;++g_monitorHWiNFODemandGeneration;demandActive=true;runJob();
 check("OFF then ON retires an already queued HWiNFO worker",managedCalls==beforeOff&&g_monitorHWiNFORecoveryResult==15&&!g_monitorHWiNFOManagementPending);
 consume(afterWake,stall);check("retired demand completion cannot consume the new admission",afterWake.attempts==0&&!afterWake.waiting&&g_nativeHWiNFORecoveryArmed);
 demandActive=false;check("no monitoring demand admits no automatic worker",!monitorLaunchHWiNFORecovery()&&jobs.empty());demandActive=true;while(!jobs.empty())runJob();g_monitorHWiNFORecoveryResult=-1;
 queueAvailable=false;check("queue failure releases pending ownership",!monitorLaunchHWiNFORecovery()&&!g_monitorHWiNFOManagementPending&&jobs.empty());queueAvailable=true;
 managementThrows=true;powerGeneration=4;monitorLaunchHWiNFORecovery();runJob();check("exception completion retains both epochs and releases pending ownership",g_monitorHWiNFORecoveryGeneration==4&&g_monitorHWiNFORecoveryDemandGeneration==g_monitorHWiNFODemandGeneration&&g_monitorHWiNFORecoveryResult==1&&!g_monitorHWiNFOManagementPending);managementThrows=2;g_monitorHWiNFORecoveryResult=-1;monitorLaunchHWiNFORecovery();bool escaped=false;try{runJob();}catch(...){escaped=true;}
 check("unknown worker exception still completes the slow recovery lane",!escaped&&g_monitorHWiNFORecoveryResult==1&&!g_monitorHWiNFOManagementPending);managementThrows=0;
#if HW_HAS_ELEVATION_RESULT
 managedResult={5,true,ERROR_CANCELLED,"inert denied elevation",{}};managedResult.elevationAttempted=true;
 g_monitorHWiNFORecoveryElevationAllowed=true;monitorLaunchHWiNFORecovery();runJob();check("failed elevation disables repeated automatic prompts",!g_monitorHWiNFORecoveryElevationAllowed);
 managedResult={5,true,5,"inert still needs permission",{}};monitorLaunchHWiNFORecovery();runJob();check("later slow recovery still runs with elevation withheld",!lastElevationAllowed&&!g_monitorHWiNFORecoveryElevationAllowed);
 managedResult={0,false,0,"inert continuing producer",{}};monitorLaunchHWiNFORecovery();runJob();check("actual successful recovery re-arms later elevation",g_monitorHWiNFORecoveryElevationAllowed);
#endif
 ymcc::hwinfo::Request request;
 for(const char* at:{"acquire","healthy","launch","second-proof"}){
  TransactionOps ops;ops.invalidate=at;ops.healthyValue=std::string(at)=="healthy";auto res=ymcc::hwinfo::transaction(ops,request);
  if(std::string(at)=="acquire")check("cancellation after mutex admission blocks all producer work",res.code==15&&!ops.saw("healthy")&&!ops.saw("resolve")&&ops.released==1);
  if(std::string(at)=="healthy")check("cancellation during healthy shortcut blocks affinity mutation",res.code==15&&!ops.saw("affinity")&&ops.released==1);
  if(std::string(at)=="launch")check("cancellation after launch cannot enter the proof loop",res.code==15&&!ops.saw("exited")&&ops.released==1);
  if(std::string(at)=="second-proof")check("cancelled final proof cannot report producer recovery",res.code==15&&ops.proofCount==2&&ops.released==1);
 }
 {TransactionOps ops;auto res=ymcc::hwinfo::transaction(ops,request);check("uncancelled recovery still requires two advancing producer observations",res.code==0&&ops.proofCount==2&&ops.released==1);}
 elevationTest::cancelBeforeElevation=true;elevationTest::cancelFromTransaction=false;elevationTest::shellCalls=0;auto elev=elevationTest::invoke(true);
 check("cancellation before elevation never launches a privileged child",elev.code==15&&elevationTest::shellCalls==0);
 elevationTest::cancelBeforeElevation=false;elevationTest::cancelFromTransaction=false;elevationTest::shellCalls=0;elev=elevationTest::invoke(false);
 check("slow background retry can withhold another UAC prompt",elev.needsElevation&&elevationTest::shellCalls==0);
 const auto shellBefore=elevationTest::shellCalls;elev=elevationTest::invoke(true);check("explicitly permitted elevation still reaches the existing launch boundary",elevationTest::shellCalls==shellBefore+1&&elev.win32==ERROR_CANCELLED);
 elevationTest::shellReturnsChild=true;elevationTest::parentProofAvailable=false;
 elev=elevationTest::invoke(true);check("elevated child success cannot mask inaccessible parent shared memory",elev.code==7);
 elevationTest::parentProofAvailable=true;elev=elevationTest::invoke(true);check("elevated recovery succeeds only after parent reads advancing producer",elev.code==0&&elev.detail=="elevated-parent-producer-confirmed");
 elevationTest::shellReturnsChild=false;
 {launchTest::Operations ops;launchTest::shellError=ERROR_ELEVATION_REQUIRED;bool elevated=false;
  try{ops.launch(L"C:\\Program Files\\HWiNFO64\\HWiNFO64.exe");}catch(const ymcc::hwinfo::Error&e){elevated=e.code==6&&e.elevation&&e.win32==ERROR_ELEVATION_REQUIRED;}
  check("actual HWiNFO shell launcher forwards error 740 to elevation",elevated&&!ops.producer);
  launchTest::shellError=0;ops.launch(L"C:\\Program Files\\HWiNFO64\\HWiNFO64.exe");
  check("actual HWiNFO launch keeps hidden shell semantics and exact process handle",bool(ops.producer)&&launchTest::hidden&&launchTest::handleRequested&&launchTest::silent&&launchTest::verb==L"open"&&launchTest::affinityApplied);
  check("HWiNFO launch preserves image and working directory with spaces",launchTest::path==L"C:\\Program Files\\HWiNFO64\\HWiNFO64.exe"&&launchTest::directory==L"C:\\Program Files\\HWiNFO64");
 }
 for(DWORD win32:{DWORD(ERROR_ACCESS_DENIED),DWORD(ERROR_PRIVILEGE_NOT_HELD),DWORD(ERROR_ELEVATION_REQUIRED),DWORD(ERROR_CANCELLED),DWORD(ERROR_FILE_NOT_FOUND),DWORD(ERROR_BAD_EXE_FORMAT)}){
  bool classified=false;try{ymcc::hwinfo::winFail(6,"inert shell failure",win32);}catch(const ymcc::hwinfo::Error&e){classified=e.elevation;}
  if(win32==ERROR_ELEVATION_REQUIRED)check("shell error 740 enters same-image elevation recovery",classified);
  else check("other shell failures preserve their permission classification",classified==(win32==ERROR_ACCESS_DENIED||win32==ERROR_PRIVILEGE_NOT_HELD));
 }
#if HW_HAS_SHELL_APARTMENT
 APTTYPE apt;APTTYPEQUALIFIER qualifier;const auto beforeApartment=CoGetApartmentType(&apt,&qualifier);
 {ymcc::hwinfo::ShellApartment scoped;const auto during=CoGetApartmentType(&apt,&qualifier);check("uninitialized recovery worker gains a real STA for shell launch",SUCCEEDED(scoped.status)&&SUCCEEDED(during)&&(apt==APTTYPE_STA||apt==APTTYPE_MAINSTA));}
 check("recovery shell apartment releases only its own COM reference",beforeApartment==CO_E_NOTINITIALIZED&&CoGetApartmentType(&apt,&qualifier)==CO_E_NOTINITIALIZED);
 const auto existing=CoInitializeEx(nullptr,COINIT_MULTITHREADED);
 {ymcc::hwinfo::ShellApartment scoped;check("existing worker apartment remains owned by its original caller",scoped.status==RPC_E_CHANGED_MODE&&SUCCEEDED(CoGetApartmentType(&apt,&qualifier))&&apt==APTTYPE_MTA);}
 check("recovery does not uninitialize another owner's MTA",SUCCEEDED(CoGetApartmentType(&apt,&qualifier))&&apt==APTTYPE_MTA);if(SUCCEEDED(existing))CoUninitialize();
#endif

 {RetryBudget debtBudget;StalledProducer debtStall;g_powerLifecycle=PowerLifecycle::Ready;g_poolCancel=false;g_monitorHWiNFOManagementPending=false;consume(debtBudget,debtStall,true);g_nativeHWiNFORecoveryArmed=false;debtBudget.started();publish(16,powerGeneration,g_monitorHWiNFODemandGeneration);consume(debtBudget,debtStall,true);check("code16 with healthy shared data cannot schedule or admit a restart",!debtBudget.waiting&&!g_nativeHWiNFORecoveryArmed);debtBudget.started();publish(16,powerGeneration,g_monitorHWiNFODemandGeneration);consume(debtBudget,debtStall,false);check("code16 without readable data retains original thirty second failure cooldown",debtBudget.waiting&&debtBudget.retryAt==tick+30000);}
 elevationTest::shellReturnsChild=true;elevationTest::parentProofAvailable=true;elevationTest::childResultCode=16;elev=elevationTest::invoke(true);check("actual elevated completion preserves16 and separately confirms parent data",elev.code==16&&elev.dataReady&&elev.sensorPolicy=="unconfirmed-elevated-journal");elevationTest::parentProofAvailable=false;elev=elevationTest::invoke(true);check("elevated16 alone cannot manufacture parent data readiness",elev.code==7&&!elev.dataReady);elevationTest::childResultCode=0;elevationTest::shellReturnsChild=false;
}catch(const std::exception&e){error=e.what();ok=false;}
std::ofstream(argv[1])<<json({{"ok",ok},{"error",error},{"cases",checks},{"events",events},{"managedCalls",managedCalls},{"hardwareWrites",false}}).dump(2);return ok?0:3;}
