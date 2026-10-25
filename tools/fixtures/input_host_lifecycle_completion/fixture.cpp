#include <atomic>
#include <cstdlib>
#include <deque>
#include <exception>
#include <iostream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
#include "json.hpp"
@@HEADER@@
using json=nlohmann::json;using ULONGLONG=unsigned long long;
static std::atomic<bool> failAllocation{false},armAuthorityAllocation{false};static unsigned allocationFailures=0;
// Arm only after the real snapshot takes its authority lock: Checked STL can
// allocate empty-string iterator proxies before the production snapshot copy.
struct InputHostMutex {
    std::mutex value;
    void lock(){value.lock();if(armAuthorityAllocation.exchange(false))failAllocation=true;}
    void unlock() noexcept {value.unlock();}
};
void* operator new(std::size_t n){if(failAllocation.exchange(false)){++allocationFailures;throw std::bad_alloc();}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}
static std::atomic<bool> g_inputHostLifecycleWorkRequested{false},g_inputHostLifecycleWorkInFlight{false},g_externalPriorityVirtualSuspended{false};
static std::atomic<unsigned long long> g_inputHostPresentationWorkGeneration{0};static std::mutex g_inputHostLifecycleWorkMx;static std::deque<std::string>g_inputHostLifecycleWorkQueue;
static InputHostMutex g_inputHostMx;static unsigned long long g_inputHostEpoch=3;static std::string g_inputHostRunId(1024,'r');
static int busyDepth=0,processed=0,workerFaults=0;static bool failBusyEnter=false,failBusyLeave=false,failDequeued=false,failAfter=false,failWork=false,changeHost=false,failSuppressionLog=false;
static std::vector<std::string> consumed;static const char* kYmccSlotStepReason="slot-step";
static void recoverySnapLifecycleEnter(const char*){++busyDepth;if(failBusyEnter){failBusyEnter=false;throw std::bad_alloc();}}
static void recoverySnapLifecycleLeave(){--busyDepth;if(failBusyLeave){failBusyLeave=false;throw std::runtime_error("diagnostic-leave");}}
@@RECOVERY_GUARD@@
@@READ_HOST@@
namespace ymcc {namespace inputHost {namespace pipeTransport {struct Runtime{void maintenance(){}};static Runtime* existingProcessRuntime(){return nullptr;}}}enum class StartupInputState{Idle,Waiting};}
static std::atomic<ymcc::StartupInputState>g_startupInputState{ymcc::StartupInputState::Idle};
static ULONGLONG GetTickCount64(){return 1000;}
static void externalControllerPriorityMaintenance(){}static void msiCrashRecoveryMaintenance(ULONGLONG){}static void msiOwnedReturnMaintenance(ULONGLONG){}static void msiTerminalReturnMaintenance(ULONGLONG){}static void msiVisibilityObserverWakeMaintenance(){}static void msiNativeForegroundMaintenance(){}static void gamepadDriverSetupPollAndPublish(){}static void inputHostRecoveryMaintenanceTick(){}
static void ymccLogThreadRole(const char*){}static void recoverySnapNotePhase(const std::string&){}static std::string inputHostConfiguredPersona(){return "steamdeck";}
static bool inputHostLifecycleWorkIsCleanup(const std::string& s){return s=="virtual-target-disabled"||s=="quarantine-endpoint-absent"||s=="frame-receipt-fault"||s.rfind("fault-stop:",0)==0;}
static void ymccSlotHandoffPlanClear(const char*,bool){}static void appendNativeLifecycleLog(const char*,const json&){if(failSuppressionLog){failSuppressionLog=false;throw std::bad_alloc();}}
static void inputHostSwitchAuditDequeued(const std::string&){if(failDequeued){failDequeued=false;throw std::bad_alloc();}}
static void msiOwnedReturnObserveOffIntent(){}static void msiNativeForegroundUiEvent(bool){}
static void focusIsolationNoteHostGenerationChanged(unsigned long long,const std::string&){if(failAfter){failAfter=false;throw std::runtime_error("post-dispatch-observer");}}
static void inputCaptureWorkerFault(const char*,const char*) noexcept{++workerFaults;}static void recordStartupInputAttempt(bool){}static bool inputHostQueueLifecycleWork(const char* s){g_inputHostLifecycleWorkQueue.push_back(s);g_inputHostLifecycleWorkRequested=true;return true;}
static void dispatchTestWork(const std::string& s){++processed;consumed.push_back(s);if(changeHost)++g_inputHostEpoch;if(failWork){failWork=false;throw std::runtime_error("uncertain-device-terminal");}}
@@WORKER@@
static unsigned checks=0;static json cases=json::array(),failures=json::array();static const char* currentCase="";static std::string caseFilter;
static void require(bool b,const char* msg){++checks;if(!b)failures.push_back({{"case",currentCase},{"assertion",msg}});}
static void reset(std::initializer_list<const char*> work){armAuthorityAllocation=false;failAllocation=false;allocationFailures=0;busyDepth=processed=workerFaults=0;failBusyEnter=failBusyLeave=failDequeued=failAfter=failWork=changeHost=failSuppressionLog=false;consumed.clear();g_inputHostEpoch=3;g_inputHostLifecycleWorkQueue.clear();for(auto s:work)g_inputHostLifecycleWorkQueue.push_back(s);g_inputHostLifecycleWorkRequested=!g_inputHostLifecycleWorkQueue.empty();g_inputHostLifecycleWorkInFlight=false;g_externalPriorityVirtualSuspended=false;}
static bool invoke(){try{inputHostRunQueuedLifecycleWork();return true;}catch(...){return false;}}
template<class F>static void run(const char* name,F body){if(!caseFilter.empty()&&caseFilter!=name)return;currentCase=name;const auto start=failures.size();body();cases.push_back({{"name",name},{"passed",failures.size()==start}});}
int main(int argc,char**argv){_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);if(argc==2)caseFilter=argv[1];
 run("ordinary-owned-work",[]{reset({"persona-transition"});require(invoke(),"normal dispatch returns");require(processed==1&&busyDepth==0,"actual scope balances after ordinary dispatch");require(!g_inputHostLifecycleWorkInFlight&&!g_inputHostLifecycleWorkRequested,"normal work completes its claim");});
 run("actual-runid-copy-allocation-failure",[]{reset({"virtual-target-disabled"});armAuthorityAllocation=true;require(invoke(),"authority snapshot allocation failure does not escape and stop capture worker");require(allocationFailures==1,"failure reached actual std::string copy in production host snapshot");require(!g_inputHostLifecycleWorkInFlight,"failed pre-dequeue snapshot releases the lifecycle claim");require(g_inputHostLifecycleWorkRequested&&g_inputHostLifecycleWorkQueue.size()==1,"pending physical return remains scheduled");require(invoke()&&processed==1&&consumed[0]=="virtual-target-disabled","next owner pass completes original cleanup after transient memory failure");require(busyDepth==0,"recovery busy depth balanced after failure");});
 run("dequeued-observation-failure",[]{reset({"virtual-target-disabled","power-rearm:test"});failDequeued=true;require(invoke(),"dequeue diagnostic failure remains inside worker boundary");require(!g_inputHostLifecycleWorkInFlight,"dequeue diagnostic releases claim");require(g_inputHostLifecycleWorkQueue.size()==2&&g_inputHostLifecycleWorkQueue.front()=="virtual-target-disabled","unattempted cleanup returns ahead of rearm");require(invoke()&&consumed.size()==1&&consumed[0]=="virtual-target-disabled","original cleanup actually dispatches on next pass");require(invoke()&&consumed.size()==2&&consumed[1]=="power-rearm:test","later work remains ordered");});
 run("recovery-observer-enter-is-optional",[]{reset({"virtual-target-disabled"});failBusyEnter=true;require(invoke(),"optional busy-enter failure does not escape");require(processed==1&&busyDepth==0,"observer enter failure cannot stop control or leak busy depth");require(!g_inputHostLifecycleWorkInFlight,"observer failure releases claim");});
 run("recovery-observer-leave-is-optional",[]{reset({"virtual-target-disabled"});failBusyLeave=true;require(invoke(),"optional busy-leave failure does not escape");require(processed==1&&busyDepth==0&&!g_inputHostLifecycleWorkInFlight,"leave failure preserves completed operation and balanced claim");});
 run("post-dispatch-failure-does-not-replay-device-work",[]{reset({"persona-transition","virtual-target-disabled"});changeHost=true;failAfter=true;require(invoke(),"generation observer failure cannot terminate capture owner");require(processed==1&&g_inputHostLifecycleWorkQueue.size()==1,"uncertain/completed device operation is not requeued");require(!g_inputHostLifecycleWorkInFlight&&g_inputHostLifecycleWorkRequested,"remaining cleanup can acquire next claim");require(invoke()&&processed==2&&consumed[1]=="virtual-target-disabled","cleanup still executes after observer failure");});
 run("business-failure-keeps-existing-no-replay-policy",[]{reset({"persona-transition","virtual-target-disabled"});failWork=true;require(invoke(),"existing business error boundary retained");require(processed==1&&workerFaults==1,"failed operation is recorded once");require(g_inputHostLifecycleWorkQueue.size()==1&&g_inputHostLifecycleWorkQueue.front()=="virtual-target-disabled","partly attempted operation not automatically repeated");require(invoke()&&processed==2,"remaining lifecycle cleanup runs");});
 run("suppression-observation-failure",[]{reset({"persona-transition","virtual-target-disabled"});g_externalPriorityVirtualSuspended=true;failSuppressionLog=true;require(invoke(),"suppression log failure is contained");require(!g_inputHostLifecycleWorkInFlight&&g_inputHostLifecycleWorkRequested,"suppression cannot strand queue claim");require(invoke()&&processed==0,"recovered external-priority request stays suppressed");require(invoke()&&processed==1&&consumed[0]=="virtual-target-disabled","external-priority suppression never blocks cleanup");});
 std::cout<<json{{"status",failures.empty()?"pass":"fail"},{"checks",checks},{"cases",cases},{"failures",failures},{"actualClaimDequeueAndFinalization",true},{"actualStdStringAllocationFailure",true},{"deviceDispatch","inert terminal replacing production device-dispatch try body only"},{"hardwareIo",false}}.dump(2)<<'\n';return failures.empty()?0:1;
}

