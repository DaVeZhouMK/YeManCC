#define NOMINMAX
#include <windows.h>
#include <winevt.h>
#include <atomic>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <deque>
#include <functional>
#include <system_error>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include "sleep_cancel_ready.h"
#include "sleep_power_state.h"
#include "sleep_power_dispatch.h"
#include "sleep_wake_admission.h"
#include <set>
#include <new>
#include <cstdlib>
static thread_local bool checkedFailNextAllocation=false;
static std::atomic<bool> checkedAllocationArmed{false},checkedIoFailureArmed{false};
static std::atomic<unsigned> checkedAllocFailures{0},checkedIoFailures{0},checkedOpens{0},checkedCloses{0};
static std::atomic<size_t> checkedFailedAllocationBytes{0};
static std::set<HANDLE> checkedHandles;
static bool checkedWrongGeneration=false,checkedPermanentIo=false;
static std::atomic<bool> checkedCaptureVetoArmed{false};
static std::atomic<ULONGLONG> checkedFirstIoTick{0},checkedLastIoTick{0};
static void checkedAfterRead(DWORD);
static void checkedFailureInput();
void* operator new(size_t n){if(checkedFailNextAllocation){checkedFailNextAllocation=false;++checkedAllocFailures;checkedFailedAllocationBytes=n;throw std::bad_alloc();}if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,size_t) noexcept {std::free(p);}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {std::free(p);}
void operator delete[](void* p,size_t) noexcept {std::free(p);}
static HANDLE WINAPI checkedCreateFile(LPCWSTR p,DWORD a,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD disposition,DWORD attrs,HANDLE tmpl){const auto h=::CreateFileW(p,a,share,security,disposition,attrs,tmpl);if(h!=INVALID_HANDLE_VALUE){checkedHandles.insert(h);++checkedOpens;if(checkedAllocationArmed.exchange(false))checkedFailNextAllocation=true;}return h;}
static BOOL WINAPI checkedClose(HANDLE h){const BOOL ok=::CloseHandle(h);if(ok){checkedHandles.erase(h);++checkedCloses;}return ok;}
static BOOL WINAPI checkedRead(HANDLE h,LPVOID buffer,DWORD n,LPDWORD got,LPOVERLAPPED overlap){if(checkedPermanentIo||checkedIoFailureArmed.exchange(false)){++checkedIoFailures;const auto now=GetTickCount64();ULONGLONG zero=0;checkedFirstIoTick.compare_exchange_strong(zero,now);checkedLastIoTick=now;checkedFailureInput();SetLastError(ERROR_READ_FAULT);return FALSE;}const BOOL ok=::ReadFile(h,buffer,n,got,overlap);if(ok&&got)checkedAfterRead(*got);return ok;}
using json=nlohmann::json;
static json existingBatchAttemptFacts=json::array(),existingBatchProgressFacts=json::array();
static bool checkedExistingReplace=false;
#include "PowerLifecycle.inc"
#include "SgSleepMode.inc"
#include "SgRetryKind.inc"
#include "SgSleepTask.inc"
#include "SgSleepTarget.inc"
#include "SgResumeResult.inc"
#include "SgWork.inc"
#include "SgWorkItem.inc"
static std::atomic<bool> g_exitRequested{false},g_inputReady{false},g_sensorSuspendCallbackActive{true};
static std::atomic<bool> g_sgInSuspend{true},g_sgGameActuallySuspended{true},g_sgSleepCycleActive{true};
static std::atomic<bool> g_hardwareWriteGate{false};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Suspending};
static std::atomic<unsigned long long> g_powerGeneration{11},g_resumeReadyGeneration{0};
static std::atomic<ULONGLONG> g_sgOriginalPowerButtonWakeFileTime{300},g_sgLastPowerButtonWakeEventFileTime{300};
static std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{200};
static std::atomic<ULONGLONG> g_sgLatestSleepEntryFileTime{200},g_sgLatestUserSleepEntryFileTime{200};
static std::atomic<ULONGLONG> g_sgLastKernel506EventFileTime{0},g_sgLastKernel507EventFileTime{0};
static std::atomic<int> g_sgLastKernel506Reason{-1},g_sgLastKernel507Reason{-1};
static SgSleepTask g_sgTask{11,SgSleepMode::S3,SgRetryKind::EntryFailure,1,200,false};
static bool g_guardEnabled=true,g_sgPauseResume=true;
static std::mutex g_sgWorkMx;
static std::condition_variable g_sgWorkCv;
static std::deque<SgWorkItem> g_sgWorkQ;
static bool g_sgWorkStop=false;
static unsigned long long g_sgWorkWakeRevision=0;
static SgSleepTarget g_sgSleepTarget{123,456,1,11,true,true};
#define ownedTarget g_sgSleepTarget
static std::mutex g_sgSleepTargetMx;
static ymcc::sleeppower::CancellationReady g_sgCancellationReady;
static struct {bool isolated=false;} g_aiFanMockSession;
static bool markerFile=true,testManualMarker=false,otherManualMarker=false;
static std::string transientVetoKind,transientVetoStage;
static bool transientVetoApplied=false;
static void applyTransientVeto();
static bool ownedLeaseMode=false;
static HANDLE ownedLeaseReady=nullptr;
static std::vector<LONG> ownedResumeStatuses;
static ULONGLONG ownedBirth(HANDLE process){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(process,&c,&e,&k,&u))return 0;ULARGE_INTEGER v{};v.LowPart=c.dwLowDateTime;v.HighPart=c.dwHighDateTime;return v.QuadPart;}
static int suspendCount=1;
static unsigned ntCalls=0,focusCalls=0,markerRemovals=0,actualOwnedNtCalls=0;
static std::atomic<unsigned> workerAutoPasses{0},workerUnexpectedActions{0},workerMarkerReadFailures{0};
static std::atomic<bool> workerReadFailureArmed{false};
static bool workerReplaceMarker=false;static std::atomic<unsigned> workerMarkerReplacements{0};
static bool workerReadFailureOnWorker=false,workerCompletion=false,workerCompletionOutsideOperation=false,workerJoinReturned=false;
static std::thread g_sgWorkThread;
static std::thread::id fixtureWorkerId;
static std::atomic<bool> fixtureWorkerRoleReady{false};
static unsigned actualKernelNtCallsOnWorker=0;
static std::mutex workerEvidenceMx;static std::condition_variable workerEvidenceCv;
static std::atomic<bool> g_sgWakeRecoveryPending{false};
static std::atomic<DWORD> g_manualPausedPid{0};
static std::mutex g_gameRulesMx;
static std::atomic<bool> keepResumeFailure{false};
static std::wstring SG_DIR,SG_SLEEP_LEASE_DIR=L"sleep",SG_MANUAL_DIR=L"manual";
static std::mutex g_sgOpMtx;
static ymcc::sleeppower::Dispatch g_sleepPowerDispatch;
static HWND g_hwnd=reinterpret_cast<HWND>(1);
static constexpr UINT_PTR SG_RETRY_TIMER_ID=1234;
static constexpr UINT WM_SG_S0_INTENT=WM_APP+1,WM_SG_S0_WAKE=WM_APP+2,WM_SG_S4_WAKE=WM_APP+3,WM_FAN_KERNEL_POWER_FACT=WM_APP+4;
struct FanKernelPowerFact{int eventId;ULONGLONG fileTime;};
static bool g_sgSleepIntentArmed=true,g_sgSleepQueryOwnsGeneration=true,g_sgSleepQueryGateClosed=true;
static bool g_sgModernStandbyActive=true,g_sgModernWakeClassified=false,g_sgRepairEligible=true,g_sgRetryInProgress=true;
static ULONGLONG g_sgSleepQueryGeneration=11,g_sgModernWakeClassifiedTick=0,g_sgRetryDueTick=1,g_sgSleepTriggerTick=1;
static double g_sgSleepTriggerEpoch=1;
static unsigned resumeCalls=0,rearmCalls=0,readyNotices=0,gateOpenCalls=0;
static std::string selectedStage,selectedEvent;
static std::mutex latchMutex;
static std::condition_variable latchCv;
static bool hookReached=false,eventPublished=false;
static std::thread::id consumerThread;
static bool operationHeld=false,operationGate=false;
static void barrier(const char* stage){
    if(std::this_thread::get_id()!=consumerThread)return;
    if(selectedStage=="operation-wait"&&std::string(stage)=="retry-stop"){
        std::lock_guard lock(latchMutex);hookReached=true;latchCv.notify_all();return;
    }
    if(selectedStage!=stage)return;
    std::unique_lock lock(latchMutex);hookReached=true;latchCv.notify_all();
    if(!latchCv.wait_for(lock,std::chrono::seconds(2),[]{return eventPublished;}))throw std::runtime_error("publisher did not complete");
}
static void traceLog(const char* format,...){if(std::string(format).find("power gate opened")!=std::string::npos){++gateOpenCalls;barrier("gate-open-log");}else barrier("trace-log");}
static void sgRecordFact(const char* kind,const json& detail){if(std::string(kind)=="sleep-game-recovery-attempt")existingBatchAttemptFacts.push_back(detail);if(std::string(kind)=="sleep-game-recovery-progress")existingBatchProgressFacts.push_back(detail);if(workerReplaceMarker&&std::string(kind)=="sleep-game-resume"&&ownedTarget.markerOwned){std::ofstream f{std::filesystem::path(SG_SLEEP_LEASE_DIR)/(std::to_wstring(ownedTarget.pid)+L".txt")};f<<"created="<<ownedTarget.processCreated<<"|state=suspended|generation=11|replacement=true\n";++workerMarkerReplacements;}}
static ULONGLONG sgNowFileTime(){return 1000;}
static std::string sgFormatFactFileTime(ULONGLONG){return "original-time";}
#include "currentPowerGeneration.inc"
#include "sgPublishSleepEventTime.inc"
#include "sgRememberKernelSleepEntry.inc"
#include "openHardwareWriteGate.inc"
static void sgStopResumeClassificationWait(const char*){barrier("classification-stop");}
static void sgSleepTimeoutStop(const char*){barrier("timeout-stop");}
static void sgClearInternalSleepRequest(const char*){barrier("internal-clear");}
static void sgClearUserStandby(const char*){barrier("standby-clear");}
static void sgStopSleepRetry(){g_sgRetryInProgress=false;g_sgTask.retryKind=SgRetryKind::None;barrier("retry-stop");}
#include "sgReadSleepTarget.inc"
#include "sgClearSleepTargetIfMatches.inc"
#include "sgSleepLeaseOwnedByGeneration.inc"
namespace fspath {
static bool exists(const std::wstring& p,std::error_code& ec){ec.clear();if(p.starts_with(SG_MANUAL_DIR)){barrier("manual-marker-read");return testManualMarker;}barrier("lease-file-exists");return ownedLeaseMode?std::filesystem::exists(p,ec):markerFile;}
static bool remove(const std::wstring&,std::error_code& ec){ec.clear();if(ownedLeaseMode){const bool result=std::filesystem::remove(std::wstring(SG_SLEEP_LEASE_DIR+L"\\"+std::to_wstring(ownedTarget.pid)+L".txt"),ec);if(result)++markerRemovals;return result;}if(markerFile){markerFile=false;++markerRemovals;}return true;}
}
namespace ActualMarkerReaders {
#define CreateFileW checkedCreateFile
#define ReadFile checkedRead
#define CloseHandle checkedClose
#include "sgReadFile.inc"
#include "sgReadResumeMarkerChecked.inc"
#undef CreateFileW
#undef ReadFile
#undef CloseHandle
}
static bool sgReadResumeMarkerChecked(const std::wstring& p,std::string& text){barrier("marker-read");if(ownedLeaseMode)return ActualMarkerReaders::sgReadResumeMarkerChecked(p,text);text="created=456|state=suspended|generation=11";return markerFile;}
static bool nativeValveValidateOwnedSuspendedTarget(const std::wstring&,DWORD,ULONGLONG);
static bool sgResumeMarkerDefinitelyStale(const std::wstring&,DWORD){return false;}
static void sgInitNt(){}
static bool sgSleepRetryActive(){return g_sgRetryInProgress;}
static bool focusQueryProcessIdentity(DWORD pid,void*,ULONGLONG* created){barrier("identity-query");if(ownedLeaseMode){HANDLE p=::OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);*created=p?ownedBirth(p):0;if(p)::CloseHandle(p);return *created!=0;}*created=456;return pid==123;}
static void focusSingleResumedGame(const std::vector<DWORD>& pids,ULONGLONG,unsigned long long){if(!pids.empty())++focusCalls;}
static HANDLE fakeOpenProcess(DWORD access,BOOL inherit,DWORD pid){barrier("process-open");const auto h=ownedLeaseMode?::OpenProcess(access,inherit,pid):reinterpret_cast<HANDLE>(2);if(ownedLeaseMode&&!keepResumeFailure&&transientVetoStage=="open")applyTransientVeto();return h;}
static BOOL fakeCloseHandle(HANDLE h){return ownedLeaseMode?::CloseHandle(h):TRUE;}
static ULONGLONG nativeProcessCreatedFromHandle(HANDLE h){barrier("opened-birth");const auto value=ownedLeaseMode?ownedBirth(h):456;if(ownedLeaseMode&&!keepResumeFailure&&transientVetoStage=="birth")applyTransientVeto();return value;}
static void fakeSleep(DWORD){barrier("resume-delay");}
static LONG fakeNtResume(HANDLE h){++ntCalls;if(ownedLeaseMode&&keepResumeFailure){ownedResumeStatuses.push_back(-1);return -1;}if(ownedLeaseMode){using Fn=LONG(NTAPI*)(HANDLE);const auto fn=reinterpret_cast<Fn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"),"NtResumeProcess"));if(!fn)return -1;++actualOwnedNtCalls;if(std::this_thread::get_id()==fixtureWorkerId)++actualKernelNtCallsOnWorker;const LONG status=fn(h);if(actualOwnedNtCalls==1&&transientVetoStage=="nt")applyTransientVeto();ownedResumeStatuses.push_back(status);if(status>=0)++resumeCalls;return status;}if(keepResumeFailure||suspendCount<=0)return -1;--suspendCount;++resumeCalls;barrier("nt-return");return 0;}
static auto fnNtResume=&fakeNtResume;
#define OpenProcess fakeOpenProcess
#define CloseHandle fakeCloseHandle
#define Sleep fakeSleep
#include "sgDrainProcessSuspendCountIfCurrent.inc"
#include "sgDrainProcessSuspendCount.inc"
#include "resume_delays.inc"
namespace WakeGameResume {
struct ThreadState {bool known=false;unsigned total=0,suspended=0;bool allRunning()const{return known&&total&&suspended==0;}};
static unsigned queryCalls=0;
static ThreadState queryThreads(DWORD){++queryCalls;barrier("running-confirmation");if(ownedLeaseMode){const bool ready=WaitForSingleObject(ownedLeaseReady,250)==WAIT_OBJECT_0;return {ready,ready?1U:0U,0};}return {selectedEvent!="unknown-running-confirmation",1,suspendCount>0?1U:0U};}
static bool sameUser(HANDLE){return false;}
#include "wake_offsets.inc"
}
#include "sgResumeSleepTargetUnlocked.inc"
#undef Sleep
#undef OpenProcess
#undef CloseHandle
#include "hardwareWriteAllowed.inc"
static std::atomic<uint64_t> g_sharedPowerPublicationRevision{0},g_sharedPowerSuspendSerial{0};
#include "readSharedSystemPower.inc"
static bool fixtureNativePowerAdmitted(){ymcc::sleeppower::SharedPowerV1 power;return ymcc::sleeppower::capturePower(&readSharedSystemPower,power);}

static BOOL fakeKillTimer(HWND,UINT_PTR){return TRUE;}
#define KillTimer fakeKillTimer
static void sleepRecoveryAutoCloseSuspendEpisode(){barrier("episode-close");}
static void inputHostRequestPowerRearm(unsigned long long,const char*){++rearmCalls;}
static void appendFanLifecycleLog(const char*,const json&){++readyNotices;}
static void fanHostScheduleEmergencyResume(unsigned long long){}
static unsigned long long currentFanPowerGeneration(){return currentPowerGeneration();}
static void ipc_emit(const char*,const json&){}
static void deckySidebarResumeAfterIntent(unsigned long long){}
static void publishSharedPowerLifecycle(const char*){}
struct EventInput{int eventId,reason,targetState;ULONGLONG originalTime;};
static thread_local EventInput incoming{506,0,0,400};
static bool sgRenderKernelPowerEvent(EVT_HANDLE,std::string& xml){xml="inert-sdk-event-leaf";return true;}
static int sgKernelPowerEventId(const std::string&){return incoming.eventId;}
static int sgKernelPowerEventDataInt(const std::string&,const char*){return incoming.targetState;}
static int sgKernelPowerReason(const std::string&){return incoming.reason;}
static ULONGLONG sgKernelPowerEventFileTime(const std::string&){return incoming.originalTime;}
namespace ymcc::abnormalsleep {static void publishConfirmedWake(std::atomic<ULONGLONG>& marker,ULONGLONG at){sgPublishSleepEventTime(marker,at);}}
namespace ymcc::sleep_residency {static json makeEvidence(const std::string&){return json::object();}}
static void sgSleepTimeoutRecordSystemSuccess(ULONGLONG){}
static void sgSleepTimeoutRecordKernel(int,int,ULONGLONG,ULONGLONG){} // Original callback cache terminal; no classification is exercised here.
static void sgStaticScanCancel(const char*){}
static void sgSleepTimeoutObserveCompleted(const json&,ULONGLONG,ULONGLONG,int){}
static json sgSleepEnvironmentSnapshot(){return json::object();}
static const char* sgKernelPowerReasonName(int){return "inert-parser-leaf";}
static void sgAbnormalPostCompleted(const std::string&,ULONGLONG){}
static void sleepRecoveryAutoRecordKernelWake(unsigned long long,int,int,int,ULONGLONG){}
static BOOL fakePostMessage(HWND,UINT message,WPARAM,LPARAM payload){
    if(message==WM_FAN_KERNEL_POWER_FACT)delete reinterpret_cast<FanKernelPowerFact*>(payload);return TRUE;
}
#define PostMessageW fakePostMessage
#include "sgKernelPowerEventCallback.inc"
static void sgAbortSleepIntent(const char*,ULONGLONG=0);
#include "sgRenewCancellationReady.inc"
#include "sgOrdinaryPauseCurrent.inc"
#include "sgReturnedPauseCurrent.inc"
#include "sgQueueReturnedPause.inc"
static bool sgHasMarkerFiles(const std::wstring& dir){std::error_code ec;if(dir==SG_MANUAL_DIR)return testManualMarker||otherManualMarker;for(const auto& e:std::filesystem::directory_iterator(dir,ec))if(e.path().extension()==L".txt")return true;return false;}
static std::string sgReadFile(const std::wstring& p){if(ownedLeaseMode&&ntCalls>=4&&workerReadFailureArmed.exchange(false)){++workerMarkerReadFailures;workerReadFailureOnWorker=std::this_thread::get_id()==fixtureWorkerId;return {};}if(p.find(L"quickapp_suspended.json")!=std::wstring::npos)return {};if(ownedLeaseMode)return ActualMarkerReaders::sgReadFile(p);std::string text;return sgReadResumeMarkerChecked(p,text)?text:std::string();}
#include "sgMarkerCreated.inc"
#include "sgMarkerIsSuspended.inc"
#include "nativeValveValidateOwnedSuspendedTarget.inc"
static bool sgWriteFileAtomic(const std::wstring& p,const std::string& s){if(p==L"controlled-trigger")return true;std::ofstream f{std::filesystem::path(p)};f<<s;return static_cast<bool>(f);}
static bool sleepRecoveryAutoAllows(unsigned long long,unsigned long long){return false;}
static bool nativeMonitorExcluded(const std::wstring&){return true;}
static std::vector<std::wstring> sgExcludes(){return {};}
static bool sgNameExcludedBy(const std::wstring&,const std::vector<std::wstring>&){return true;}
#include "sgNotifyWork.inc"
#define OpenProcess fakeOpenProcess
#define CloseHandle fakeCloseHandle
#include "wake_batch.inc"
#undef OpenProcess
#undef CloseHandle
#include "sgAbortSleepIntent.inc"
static void publishSelected(){
    if(selectedEvent=="exit")g_exitRequested.store(true,std::memory_order_release);
    else if(selectedEvent=="new-suspend")g_sleepPowerDispatch.callback(PBT_APMSUSPEND,400,[](const auto&){return true;},400);
    else if(selectedEvent=="new-query")g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,400,[](const auto&){return true;},400);
    else if(selectedEvent=="resume-only")g_sleepPowerDispatch.callback(PBT_APMRESUMEAUTOMATIC,400,[](const auto&){return true;},400);
    else {
        incoming={506,selectedEvent=="new506-user"?1:0,0,selectedEvent=="late506-old"?250ULL:400ULL};
        if(selectedEvent=="missing506")incoming.originalTime=0;
        if(selectedEvent=="future506")incoming.originalTime=2000;
        sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(1));
    }
}
static ULONGLONG g_entryFailureInterruptedAt=0,g_entryFailureQuickDeadline=0,g_sgLastS0WakeTick=0,g_sgLastPowerButtonWakeTick=0;
static bool g_entryFailureExpanded=false;
static int g_sgLastS0WakeReason=-1;
static const std::wstring SG_SLEEP_TRIGGER_MARKER=L"controlled-trigger";
static void sgBlockSharedWakeClassification(unsigned long long){}
static void closeJoyXoffAsync(const char*){}
static void sgRefreshSleepTriggerOrigin(){}
static void sgArmNonUserWakeGuard(const char*){}

static void sgSleepTimeoutObserveSuspend(){}
#include "sgMarkSleepTrigger.inc"
static int ownedLeaseChild(const char* readyName,const char* stopName){
 HANDLE ready=OpenEventA(EVENT_MODIFY_STATE,FALSE,readyName);
 HANDLE stop=OpenEventA(SYNCHRONIZE,FALSE,stopName);
 if(!ready||!stop){if(ready)CloseHandle(ready);if(stop)CloseHandle(stop);return 4;}
 const BOOL signaled=SetEvent(ready);const DWORD wait=WaitForSingleObject(stop,5000);
 CloseHandle(ready);CloseHandle(stop);return signaled&&wait==WAIT_OBJECT_0?0:5;
}
static void applyTransientVeto(){
 if(transientVetoApplied||transientVetoKind.empty())return;transientVetoApplied=true;
 if(transientVetoKind=="kernel"){incoming={506,1,0,400};sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(1));}
 else if(transientVetoKind=="suspend")g_sleepPowerDispatch.callback(PBT_APMSUSPEND,GetTickCount64(),[](const auto&){return true;},400);
 else if(transientVetoKind=="query")g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,GetTickCount64(),[](const auto&){return true;},400);
 else if(transientVetoKind=="manual")testManualMarker=true;
 else if(transientVetoKind=="owner")++ownedTarget.processCreated;
 else if(transientVetoKind=="marker"){std::ofstream f{std::filesystem::path(SG_SLEEP_LEASE_DIR)/(std::to_wstring(ownedTarget.pid)+L".txt")};f<<"created="<<ownedTarget.processCreated<<"|state=suspended|generation=11|replacement=true\n";}
 else if(transientVetoKind=="exit")g_exitRequested=true;
 else if(transientVetoKind=="generation")g_powerGeneration.fetch_add(1);
 else throw std::runtime_error("unknown transient veto");
}
// Original worker code below. Unselected power/Steam actions refuse instead of running.
static std::atomic<bool> g_sgEntryHandoffPending{false},g_sgEntryHandoffReady{false},g_sleepRecoveryAutoPending{false};
static std::atomic<unsigned long long> g_sgPauseWorkCompletedGeneration{0};
namespace ymcc::threadRole {static void describe(const wchar_t*){fixtureWorkerId=std::this_thread::get_id();fixtureWorkerRoleReady.store(true,std::memory_order_release);}}
static ULONGLONG sleepRecoveryAutoWaitMs(ULONGLONG){return ~ULONGLONG{0};}
static void sleepRecoveryAutoTick(){++workerAutoPasses;}
static void sleepRecoveryAutoGameReleased(unsigned long long){
 const bool free=g_sgOpMtx.try_lock();if(free)g_sgOpMtx.unlock();
 {std::lock_guard lock(workerEvidenceMx);workerCompletionOutsideOperation=free;workerCompletion=true;}workerEvidenceCv.notify_all();
}
#include "SgSuspendResult.inc"
#include "powerLifecycleMatches.inc"
[[noreturn]] static void workerRejectUnselected(){++workerUnexpectedActions;throw std::runtime_error("unselected original worker action");}
static SgSuspendResult sgPauseSleepTarget(unsigned long long,const SgWorkItem* =nullptr){workerRejectUnselected();}
static void sgRealWake(const char*,unsigned long long){workerRejectUnselected();}
static void publishPowerResumeReady(unsigned long long){workerRejectUnselected();}
static void sgApplyLongSleepResumeGrace(unsigned long long){workerRejectUnselected();}
static bool sgResumeClassificationHeld(unsigned long long){workerRejectUnselected();}
static bool sgResumeClassificationObserved(unsigned long long){workerRejectUnselected();}
static bool sgUserWakeCancelsSleepIntent(ULONGLONG,ULONGLONG){workerRejectUnselected();}
static bool sgShouldKeepInputDuringStandby(){workerRejectUnselected();}
static void sgStopSleepRetry(bool){workerRejectUnselected();}
static bool joinThreadBoundedForExit(std::thread& t,DWORD timeoutMs,const char*){
 if(!t.joinable())return true;if(WaitForSingleObject(t.native_handle(),timeoutMs)!=WAIT_OBJECT_0)return false;t.join();workerJoinReturned=true;return true;
}
#include "sgWaitForWork.inc"
#include "sgServiceEntryObservationHandoff.inc"
#include "sgWorkLoop.inc"
#include "sgStartWorkThread.inc"
#include "sgStopWorkThread.inc"
static void checkedAfterRead(DWORD bytes){if(bytes&&std::this_thread::get_id()==fixtureWorkerId&&checkedCaptureVetoArmed.exchange(false))applyTransientVeto();}
static void checkedFailureInput(){if(checkedExistingReplace&&ownedTarget.markerOwned){std::ofstream f{std::filesystem::path(SG_SLEEP_LEASE_DIR)/(std::to_wstring(ownedTarget.pid)+L".txt")};f<<"created="<<ownedTarget.processCreated<<"|state=suspended|generation=11|replacement=true\n";}if(checkedWrongGeneration&&ownedTarget.markerOwned){std::ofstream f{std::filesystem::path(SG_SLEEP_LEASE_DIR)/(std::to_wstring(ownedTarget.pid)+L".txt")};f<<"created="<<ownedTarget.processCreated<<"|state=suspended|generation=12\n";}}
static int runOwnedLease(const std::string& mode){
 const bool same=mode=="same-generation",continued=mode=="continued-generation"||mode.starts_with("transient-");
 const bool transient=mode.starts_with("transient-");
 const bool workerMode=mode.starts_with("transient-worker"),workerReadMode=mode.starts_with("transient-worker-read");
 const bool checkedAllocMode=mode=="transient-worker-checked-alloc",checkedIoMode=mode=="transient-worker-checked-io",checkedGenerationMode=mode=="transient-worker-checked-bad-generation";
 const bool existingMode=mode.starts_with("transient-worker-checked-batch-");
 const auto existingKind=existingMode?mode.substr(std::string("transient-worker-checked-batch-").size()):std::string{};
 checkedExistingReplace=existingKind=="merge-replaced";
 const bool checkedBoundaryMode=mode.starts_with("transient-worker-checked-veto-"),checkedPermanentMode=mode=="transient-worker-checked-permanent";
 if(checkedBoundaryMode){transientVetoKind=mode.substr(std::string("transient-worker-checked-veto-").size());transientVetoStage="capture";checkedCaptureVetoArmed=true;}
 checkedPermanentIo=checkedPermanentMode;
 checkedAllocationArmed=checkedAllocMode;checkedIoFailureArmed=checkedIoMode||checkedGenerationMode||checkedBoundaryMode||checkedPermanentMode||(existingMode&&existingKind!="known-into-empty");checkedWrongGeneration=checkedGenerationMode;
 bool abortThrewAllocation=false;
 const bool workerReplaceMode=mode=="transient-worker-replace";workerReplaceMarker=workerReplaceMode;
 const bool permanent=mode=="transient-permanent",batchAppend=mode=="transient-batch-append"||(existingMode&&existingKind=="append"),batchMerge=mode=="transient-batch-merge"||(existingMode&&existingKind!="append"),readyHold=mode=="transient-ready-hold";
 const bool veto=transient&&!workerMode&&!permanent&&!batchAppend&&!batchMerge&&!readyHold&&mode!="transient-resume";
 if(veto){const auto value=mode.substr(10);const auto split=value.find('-');transientVetoKind=value.substr(0,split);transientVetoStage=split==std::string::npos?"tick":value.substr(split+1);}
 const bool manual=mode=="manual-marker",wrongBirth=mode=="wrong-birth",newer=mode=="newer-boundary";
 if(!same&&!continued&&!manual&&!wrongBirth&&!newer)throw std::runtime_error("unknown owned lease selector");
 DWORD handlesBefore=0,handlesAfter=0,handlesAfterCreate=0,handlesBeforeAbort=0,handlesBeforeCleanup=0;GetProcessHandleCount(GetCurrentProcess(),&handlesBefore);
 const auto unique=std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64());
 const std::string readyName="Local\\YMCC_LeaseReady_"+unique,stopName="Local\\YMCC_LeaseStop_"+unique;
 HANDLE ready=CreateEventA(nullptr,TRUE,FALSE,readyName.c_str());HANDLE stop=CreateEventA(nullptr,TRUE,FALSE,stopName.c_str());
 PROCESS_INFORMATION child{};STARTUPINFOW startup{};startup.cb=sizeof(startup);
 wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);
 const auto wide=[](const std::string& s){return std::wstring(s.begin(),s.end());};
 std::wstring command=L"\""+std::wstring(exe)+L"\" --owned-lease-child "+wide(readyName)+L" "+wide(stopName);
 const bool created=ready&&stop&&CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child);
 if(!created){if(ready)CloseHandle(ready);if(stop)CloseHandle(stop);throw std::runtime_error("owned helper create failed");}
 GetProcessHandleCount(GetCurrentProcess(),&handlesAfterCreate);
 const auto actualBirth=ownedBirth(child.hProcess);
 const auto dir=std::filesystem::current_path()/wide("owned-lease-"+unique);
 std::filesystem::create_directory(dir);SG_DIR=dir.wstring();SG_SLEEP_LEASE_DIR=dir.wstring();SG_MANUAL_DIR=(dir/L"manual").wstring();
 const auto marker=dir/(std::to_wstring(child.dwProcessId)+L".txt");
 {std::ofstream f(marker,std::ios::binary);if(mode.ends_with("-bom"))f<<"\xEF\xBB\xBF";f<<"created="<<actualBirth<<"|state=suspended|generation=11\n";}
 ownedLeaseMode=true;ownedLeaseReady=ready;testManualMarker=manual;
 const auto expectedBirth=wrongBirth?actualBirth+1:actualBirth;
 ownedTarget={child.dwProcessId,expectedBirth,1,11,true,true};
 g_powerGeneration=same?11:12;
 const auto receiptTime=GetTickCount64();
 g_sleepPowerDispatch.callback(PBT_APMSUSPEND,receiptTime,[](const auto&){return true;},200);
 const auto pending=g_sleepPowerDispatch.pendingReplay();
 const auto receipt=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type,GetTickCount64());
 if(!receipt||receipt.fileTime100ns!=200||receipt.tick!=receiptTime)throw std::runtime_error("owned trigger exact receipt not claimed");
 sgMarkSleepTrigger(&receipt);g_sleepPowerDispatch.complete(receipt,GetTickCount64());
 const auto taskBefore=g_sgTask.generation;
 if(newer){incoming={506,1,0,400};sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(1));}
 GetProcessHandleCount(GetCurrentProcess(),&handlesBeforeAbort);
 ULONGLONG oldBatchStarted=0;size_t oldBatchAttempt=0;
 if(batchAppend||batchMerge){
  g_sgWakeRecovery.generation=g_powerGeneration.load()-(existingKind=="stale-batch"?1:0);
  g_sgWakeRecovery.started=GetTickCount64()-(existingKind=="stale-owner-final"?9990:existingMode?3000:1);g_sgWakeRecovery.nextAttempt=existingKind=="stale-owner-final"?4:existingMode?2:0;g_sgWakeRecovery.manualState="unrelated-manual-ui";
  SgWakeRecoveryEntry entry;entry.dir=batchAppend?SG_MANUAL_DIR:SG_SLEEP_LEASE_DIR;entry.pid=batchAppend?child.dwProcessId+1:child.dwProcessId;
  entry.created=actualBirth;entry.markerText=sgReadFile(marker.wstring());entry.sleepOwnerGeneration=batchAppend?0:11;entry.done=batchAppend;
  if(existingKind=="merge-empty"||existingKind=="known-into-empty"){entry.markerText.clear();entry.canceledLease=true;entry.canceledThrough=300;entry.canceledBoundarySequence=g_sleepPowerDispatch.observedSleepBoundarySequence();}
  if(existingKind=="merge-done")entry.done=true;
  if(existingKind=="stale-owner"||existingKind=="stale-owner-done"||existingKind=="stale-owner-final"){entry.sleepOwnerGeneration=10;const auto at=entry.markerText.find("generation=11");assert(at!=std::string::npos);entry.markerText.replace(at,13,"generation=10");}
  if(existingKind=="stale-identity"){entry.created=actualBirth-1;const auto old=std::to_string(actualBirth);const auto at=entry.markerText.find(old);assert(at!=std::string::npos);entry.markerText.replace(at,old.size(),std::to_string(entry.created));}
  if(existingKind=="stale-owner-done")entry.done=true;
  g_sgWakeRecovery.entries.push_back(entry);g_sgWakeRecoveryPending=true;oldBatchStarted=g_sgWakeRecovery.started;oldBatchAttempt=g_sgWakeRecovery.nextAttempt;
  otherManualMarker=true;g_manualPausedPid=child.dwProcessId+1;
 }
 consumerThread=std::this_thread::get_id();unsigned idleAutoPasses=0;
 if(workerMode&&!existingMode){workerReadFailureArmed=workerReadMode;sgStartWorkThread();for(unsigned i=0;i<1000&&!fixtureWorkerRoleReady.load(std::memory_order_acquire);++i)::Sleep(1);if(!fixtureWorkerRoleReady.load(std::memory_order_acquire))throw std::runtime_error("worker role did not publish");::Sleep(40);idleAutoPasses=workerAutoPasses.load();}
 keepResumeFailure=transient;try{sgAbortSleepIntent("owned-lease-cancel",300);}catch(const std::bad_alloc&){abortThrewAllocation=true;checkedFailNextAllocation=false;}
 bool queuedAfterAbort=false;SgSleepTarget leaseBeforeRetry;ULONGLONG queuedBatchStarted=0;size_t queuedBatchAttempt=0,queuedTargets=0;
 {std::lock_guard lock(g_sgOpMtx);queuedAfterAbort=g_sgWakeRecoveryPending.load();leaseBeforeRetry=ownedTarget;queuedBatchStarted=g_sgWakeRecovery.started;queuedBatchAttempt=g_sgWakeRecovery.nextAttempt;queuedTargets=g_sgWakeRecovery.entries.size();}
 keepResumeFailure=permanent;
 if(existingMode){sgStartWorkThread();for(unsigned i=0;i<1000&&!fixtureWorkerRoleReady.load(std::memory_order_acquire);++i)::Sleep(1);if(!fixtureWorkerRoleReady.load(std::memory_order_acquire))throw std::runtime_error("existing-batch worker role did not publish");}
 if(veto&&transientVetoStage=="tick")applyTransientVeto();
 if(readyHold)g_inputReady=false;
 const auto recoveryWaitAfterAbort=transient?sgWakeGameRecoveryWaitMs(GetTickCount64()):~ULONGLONG{0};
 if(workerMode){
  {std::unique_lock lock(workerEvidenceMx);workerEvidenceCv.wait_for(lock,std::chrono::milliseconds(checkedGenerationMode||checkedPermanentMode?12000:4000),[]{return workerCompletion;});}
  sgStopWorkThread(2500);
 }else if(transient)sgWakeGameRecoveryTick();
 const auto callsWhileNotReady=actualOwnedNtCalls;
 if(readyHold){g_inputReady=true;sgWakeGameRecoveryTick();}
 if(permanent){
  for(size_t i=1;i<WakeGameResume::attemptCount;++i){const auto due=queuedBatchStarted+WakeGameResume::attemptOffsetsMs[i];const auto now=GetTickCount64();if(now<due)::Sleep(static_cast<DWORD>(due-now));sgWakeGameRecoveryTick();}
 }
 keepResumeFailure=false;
 const DWORD runningWait=WaitForSingleObject(ready,250);
 const bool running=runningWait==WAIT_OBJECT_0,retained=ownedTarget.markerOwned;
 const bool markerRetained=std::filesystem::exists(marker);
 const bool resumeExpected=same||continued;
 const bool issuedBeforeVeto=veto&&transientVetoStage=="nt";
 const bool workerSafe=!workerMode||(idleAutoPasses==0&&((checkedBoundaryMode&&transientVetoKind=="exit")||(workerCompletion&&workerCompletionOutsideOperation))&&workerJoinReturned&&!g_sgWorkThread.joinable()&&workerUnexpectedActions==0);
 const bool existingVeto=existingKind=="merge-replaced"||existingKind=="merge-done";
 const bool existingClock=existingKind=="stale-batch"?(queuedBatchStarted>oldBatchStarted&&queuedBatchAttempt==0):(oldBatchStarted==queuedBatchStarted&&oldBatchAttempt==queuedBatchAttempt);
 const bool existingDistinct=existingKind=="stale-owner"||existingKind=="stale-identity"||existingKind=="stale-owner-done"||existingKind=="stale-owner-final";
 const bool existingNormal=checkedIoFailures.load()==(existingKind=="known-into-empty"?0U:1U)&&existingClock&&queuedTargets==((existingKind=="append"||existingDistinct)?2U:1U)&&g_manualPausedPid==child.dwProcessId+1&&(existingVeto?(!running&&retained&&markerRetained&&actualOwnedNtCalls==0):(running&&!retained&&!markerRetained&&actualKernelNtCallsOnWorker==8))&&!g_sgWakeRecoveryPending.load()&&focusCalls==0;
 const bool normal=!abortThrewAllocation&&workerSafe&&(existingMode?existingNormal:checkedBoundaryMode?(!running&&retained&&markerRetained&&actualOwnedNtCalls==0&&transientVetoApplied&&focusCalls==0&&(transientVetoKind=="exit"||!g_sgWakeRecoveryPending.load())):checkedPermanentMode?(!running&&retained&&markerRetained&&!g_sgWakeRecoveryPending.load()&&actualOwnedNtCalls==0&&checkedIoFailures.load()==WakeGameResume::attemptCount+1):checkedGenerationMode?(!running&&retained&&markerRetained&&!g_sgWakeRecoveryPending.load()&&actualOwnedNtCalls==0):workerReplaceMode?(!running&&retained&&markerRetained&&actualOwnedNtCalls==0&&workerMarkerReplacements==1):veto?(running==issuedBeforeVeto&&retained&&markerRetained&&actualOwnedNtCalls==(issuedBeforeVeto?1U:0U)&&focusCalls==0):permanent?(!running&&retained&&markerRetained&&!g_sgWakeRecoveryPending.load()&&actualOwnedNtCalls==0):resumeExpected?(running&&!retained&&!markerRetained&&(!readyHold||callsWhileNotReady==0)&&(!(batchAppend||batchMerge)||(oldBatchStarted==queuedBatchStarted&&oldBatchAttempt==queuedBatchAttempt&&queuedTargets==(batchAppend?2U:1U)&&g_manualPausedPid==child.dwProcessId+1))):(!running&&retained&&markerRetained&&ntCalls==0));
 const bool safe=(!wrongBirth||ntCalls==0)&&(!newer||(!fixtureNativePowerAdmitted()&&rearmCalls==0));
 const auto taskAfter=g_sgTask.generation;const auto phase=g_powerLifecycle.load();
 const auto productCalls=ntCalls;const auto productResume=resumeCalls;const auto birthText=std::to_string(actualBirth);
 GetProcessHandleCount(GetCurrentProcess(),&handlesBeforeCleanup);
 const auto productCheckedHandles=checkedHandles.size();const auto productCheckedOpens=checkedOpens.load(),productCheckedCloses=checkedCloses.load();
 bool fixtureCheckedReap=true;for(HANDLE h:checkedHandles)fixtureCheckedReap=::CloseHandle(h)!=FALSE&&fixtureCheckedReap;checkedHandles.clear();
 DWORD handlesAfterCheckedReap=0;GetProcessHandleCount(GetCurrentProcess(),&handlesAfterCheckedReap);
 // Fixture cleanup is distinct from the observed product result; it never repairs the assertion.
 const bool cleanupResume=ResumeThread(child.hThread)!=static_cast<DWORD>(-1);
 SetEvent(stop);const DWORD exitWait=WaitForSingleObject(child.hProcess,2500);DWORD exitCode=STILL_ACTIVE;
 GetExitCodeProcess(child.hProcess,&exitCode);
 const bool closedThread=CloseHandle(child.hThread)!=FALSE,closedProcess=CloseHandle(child.hProcess)!=FALSE;
 const bool closedReady=CloseHandle(ready)!=FALSE,closedStop=CloseHandle(stop)!=FALSE;
 std::error_code ec;std::filesystem::remove(marker,ec);std::filesystem::remove(dir,ec);
 GetProcessHandleCount(GetCurrentProcess(),&handlesAfter);
 const bool cleanup=cleanupResume&&exitWait==WAIT_OBJECT_0&&exitCode==0&&closedThread&&closedProcess&&closedReady&&closedStop&&handlesBefore==handlesAfter;
 const bool ownedResourcesReturned=cleanupResume&&exitWait==WAIT_OBJECT_0&&exitCode==0&&closedThread&&closedProcess&&closedReady&&closedStop&&handlesBeforeAbort==handlesBeforeCleanup&&handlesAfter+4==handlesAfterCreate;
 const json result={{"stage","owned-lease"},{"event",mode},{"callbackPublished",true},{"safe",safe},{"normal",normal},
  {"ownedPid",child.dwProcessId},{"ownedProcessCreated100ns",birthText},{"expectedProcessCreated100ns",std::to_string(expectedBirth)},
  {"leaseGeneration",11},{"taskGenerationBefore",taskBefore},{"taskGenerationAfter",taskAfter},{"nativeGeneration",g_powerGeneration.load()},
  {"ownedChildRunningBeforeFixtureCleanup",running},{"markerOwnedBeforeFixtureCleanup",retained},{"markerFileBeforeFixtureCleanup",markerRetained},
  {"existingBatchMode",existingMode},{"existingBatchKind",existingKind},{"workerStartedAfterAbort",existingMode},{"existingBatchClockPreserved",existingClock},{"existingBatchAttemptFacts",existingBatchAttemptFacts},{"existingBatchProgressFacts",existingBatchProgressFacts},{"manualStatePidAfter",g_manualPausedPid.load()},{"abortThrewAllocation",abortThrewAllocation},{"checkedAllocFailures",checkedAllocFailures.load()},{"checkedIoFailures",checkedIoFailures.load()},{"checkedFirstIoTick",std::to_string(checkedFirstIoTick.load())},{"checkedLastIoTick",std::to_string(checkedLastIoTick.load())},{"checkedFailedAllocationBytes",checkedFailedAllocationBytes.load()},{"productCheckedHandles",productCheckedHandles},{"productCheckedOpens",productCheckedOpens},{"productCheckedCloses",productCheckedCloses},{"fixtureCheckedReap",fixtureCheckedReap},{"handlesAfterCheckedReap",handlesAfterCheckedReap},{"actualOriginalLeaseValidator",true},{"actualOriginalMarkerReaders",true},{"workerMarkerReplacements",workerMarkerReplacements.load()},{"workerMode",workerMode},{"idleWorkerAutoPasses",idleAutoPasses},{"workerAutoPasses",workerAutoPasses.load()},{"workerCompletion",workerCompletion},{"workerCompletionOutsideOperation",workerCompletionOutsideOperation},{"workerJoinReturned",workerJoinReturned},{"workerUnexpectedActions",workerUnexpectedActions.load()},{"workerMarkerReadFailures",workerMarkerReadFailures.load()},{"workerReadFailureOnWorker",workerReadFailureOnWorker},{"actualKernelNtCallsOnWorker",actualKernelNtCallsOnWorker},{"queuedAfterAbort",queuedAfterAbort},{"transientVetoApplied",transientVetoApplied},{"oldBatchStarted",std::to_string(oldBatchStarted)},{"queuedBatchStarted",std::to_string(queuedBatchStarted)},{"oldBatchAttempt",oldBatchAttempt},{"queuedBatchAttempt",queuedBatchAttempt},{"queuedTargets",queuedTargets},{"callsWhileNotReady",readyHold?callsWhileNotReady:0},{"permanentFailureWindowExhausted",permanent&&!g_sgWakeRecoveryPending.load()},{"recoveryWaitAfterAbortMs",std::to_string(recoveryWaitAfterAbort)},{"leaseBeforeRetryPid",leaseBeforeRetry.pid},{"recoveryPendingAfterTick",g_sgWakeRecoveryPending.load()},{"actualKernelNtCalls",actualOwnedNtCalls},{"focusCalls",focusCalls},{"productNtCalls",productCalls},{"productSuccessfulNtReturns",productResume},{"productNtStatuses",ownedResumeStatuses},
  {"phase",static_cast<unsigned>(phase)},{"nativePowerAdmitted",fixtureNativePowerAdmitted()},
  {"fixtureCleanupResumeThread",cleanupResume},{"ownedChildExitWait",exitWait},{"ownedChildExitCode",exitCode},
  {"handlesBefore",handlesBefore},{"handlesAfterCreate",handlesAfterCreate},{"handlesBeforeAbort",handlesBeforeAbort},{"handlesBeforeCleanup",handlesBeforeCleanup},{"handlesAfter",handlesAfter},{"allFourHandlesClosed",closedThread&&closedProcess&&closedReady&&closedStop},{"cleanupComplete",cleanup},{"coldProcessBaselineEqual",handlesBefore==handlesAfter},{"ownedResourcesReturned",ownedResourcesReturned},{"coldCreateProcessHandleDelta",static_cast<int>(handlesAfterCreate)-static_cast<int>(handlesBefore)-4},
  {"markerAndIdentityTransportControlled",true},{"actualProductResumeAndAbortBodies",true},{"actualMarkSleepTriggerBody",true},{"originalDispatchReceiptClaimedAndCompleted",true},{"actualOwnedNtResumeProcess",actualOwnedNtCalls!=0},{"realSystemSleep",false},{"existingAppOrGameControlled",false}};
 std::cout<<result.dump()<<'\n';return normal&&safe&&ownedResourcesReturned?0:2;
}

int main(int argc,char** argv){try{
    if(argc==4&&std::string(argv[1])=="--owned-lease-child")return ownedLeaseChild(argv[2],argv[3]);
    if(argc==3&&std::string(argv[1])=="owned-lease")return runOwnedLease(argv[2]);
    if(argc!=3)throw std::runtime_error("stage/event required");selectedStage=argv[1];selectedEvent=argv[2];
    consumerThread=std::this_thread::get_id();
    if(selectedStage=="lifetime"){
        g_sgCancellationReady.bind(currentPowerGeneration(),300,0);
        g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=true;
        if(selectedEvent.starts_with("query"))g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPEND,400,[](const auto&){return true;},400);
        else {incoming={506,1,0,400};sgKernelPowerEventCallback(EvtSubscribeActionDeliver,nullptr,reinterpret_cast<EVT_HANDLE>(1));}
        const auto pending=g_sleepPowerDispatch.pendingReplay();
        if(pending){const auto claimed=g_sleepPowerDispatch.forwarded(pending.sequence,pending.type);g_sleepPowerDispatch.complete(claimed);}
        const bool before=fixtureNativePowerAdmitted();
        if(selectedEvent=="exit")g_exitRequested=true;
        const auto original=selectedEvent.ends_with("stale")?350ULL:selectedEvent=="entry-missing"?0ULL:selectedEvent=="entry-future"?2000ULL:450ULL;
        const bool accepted=sgRenewCancellationReady(original);
        const bool expected=selectedEvent=="entry-current"||selectedEvent=="query-current";
        const bool after=fixtureNativePowerAdmitted();
        const bool ok=!before&&accepted==expected&&after==expected;
        std::cout<<json({{"stage",selectedStage},{"event",selectedEvent},{"callbackPublished",true},{"safe",ok},{"normal",ok},{"wasAllowed",before},{"renewAccepted",accepted},{"effectiveHardwareAllowed",after},{"newGeneration",false},{"physicalCalls",0}}).dump()<<'\n';
        return ok?0:2;
    }

    keepResumeFailure=selectedStage=="resume-delay";
    if(selectedEvent=="partial-running-confirmation")suspendCount=3;
    if(selectedEvent=="late506-before-cancel"){
        g_sgTask.retryKind=SgRetryKind::None;g_sgRetryInProgress=false;
        g_sleepPowerDispatch.callback(PBT_APMQUERYSUSPENDFAILED,500,[](const auto&){return true;},500);
    }
    if(selectedStage=="before-abort"){publishSelected();eventPublished=true;}
    const bool raced=selectedStage!="none"&&selectedStage!="before-abort";
    std::thread publisher;
    if(raced){
        publisher=std::thread([]{
            std::unique_lock<std::mutex> operation(g_sgOpMtx,std::defer_lock);
            if(selectedStage=="operation-wait")operation.lock();
            std::unique_lock lock(latchMutex);operationHeld=operation.owns_lock();operationGate=true;latchCv.notify_all();
            if(!latchCv.wait_for(lock,std::chrono::seconds(2),[]{return hookReached;}))return;
            if(operationHeld)latchCv.wait_for(lock,std::chrono::milliseconds(20),[]{return false;});
            lock.unlock();publishSelected();lock.lock();eventPublished=true;lock.unlock();latchCv.notify_all();
        });
    }
    if(raced){std::unique_lock lock(latchMutex);if(!latchCv.wait_for(lock,std::chrono::seconds(2),[]{return operationGate;}))throw std::runtime_error("operation holder not ready");}
    sgAbortSleepIntent(selectedEvent=="late506-before-cancel"?"query-canceled":"inert-fixture-cancel",selectedEvent=="late506-before-cancel"?500:0);
    if(publisher.joinable())publisher.join();
    const auto phase=g_powerLifecycle.load();
    const bool superseding=selectedEvent=="new506-user"||selectedEvent=="new506-nonuser"||selectedEvent=="partial-running-confirmation"||selectedEvent=="unknown-running-confirmation"||selectedEvent=="new-suspend"||selectedEvent=="new-query"||selectedEvent=="exit";
    const bool early=selectedStage!="nt-return"&&selectedStage!="trace-log"&&selectedStage!="episode-close"&&selectedStage!="gate-open-log";
    const bool safe=!superseding||(!fixtureNativePowerAdmitted()&&!rearmCalls&&!readyNotices&&(selectedStage=="gate-open-log"||phase!=PowerLifecycle::Ready)&&(!early||resumeCalls==0));
    const bool normal=superseding||(resumeCalls==(keepResumeFailure?0U:1U)&&rearmCalls==1&&readyNotices==1&&phase==PowerLifecycle::Ready&&fixtureNativePowerAdmitted());
    const bool delivered=!raced||eventPublished;
    std::cout<<json({{"settlementQueries",WakeGameResume::queryCalls},{"rawHardwareAllowed",hardwareWriteAllowed()},{"effectiveHardwareAllowed",fixtureNativePowerAdmitted()},{"ntCalls",ntCalls},{"focusCalls",focusCalls},{"markerRemovals",markerRemovals},{"suspendCount",suspendCount},{"operationHeldAtPublication",operationHeld},{"stage",selectedStage},{"event",selectedEvent},{"callbackPublished",delivered},{"resumeCalls",resumeCalls},{"rearmCalls",rearmCalls},{"readyNotices",readyNotices},{"gateOpenCalls",gateOpenCalls},{"phase",static_cast<int>(phase)},{"safe",safe},{"normal",normal},{"originalEntry",g_sgLatestSleepEntryFileTime.load()},{"originalWake",g_sgOriginalPowerButtonWakeFileTime.load()},{"taskGeneration",g_sgTask.generation},{"markerOwned",ownedTarget.markerOwned},{"pendingSuspend",g_sleepPowerDispatch.pendingSuspend()},{"physicalCalls",0}}).dump()<<'\n';
    return delivered&&safe&&normal&&!(suspendCount>0&&!markerFile)&&!(selectedEvent=="unknown-running-confirmation"&&!ownedTarget.markerOwned)?0:2;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 3;}}
