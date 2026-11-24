"""Compile actual sleep-PID functions with deterministic OS/input boundary fakes.

No user process is suspended. In addition to the source contracts, the generated
C++ harness executes functions extracted verbatim from native/main.cpp (not a
separately reimplemented state-machine model).
Run from the repository root with Python; pass --output to retain build evidence.
"""
from pathlib import Path
import argparse
import re
import subprocess
import hashlib
import json as report_json

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=Path('native/main.cpp'))
parser.add_argument('--output', type=Path, default=Path('../../Build/Validation/SleepGamePause-20260929'))
parser.add_argument('--prepare-only',action='store_true',help='Retain exact generated sources and compile recipe without executing')
parser.add_argument('--cancel-boundary-only', action='store_true', help='Only complete original cancellation and process-loop boundaries')
parser.add_argument('--cancel-boundary-case', default=None, help='Optional exact gate-current/deep-current/partial-return subset')
parser.add_argument('--returned-pause-only',action='store_true',help='Only exact returned-resume to original worker continuation cases')
parser.add_argument('--returned-pause-bottom-only',action='store_true',help='Same returned continuation cases with complete original suspend helper')
parser.add_argument('--returned-pause-case',default=None,help='Exact one original continuation scenario')
parser.add_argument('--ordinary-pause-only',action='store_true',help='Only actual queue and original full suspend path human-wake boundaries')
parser.add_argument('--ordinary-pause-case',default=None,help='One exact ordinary-pause boundary')
args = parser.parse_args()
if args.ordinary_pause_case is not None and not args.ordinary_pause_only:parser.error('ordinary-pause-case requires ordinary-pause-only')
if args.ordinary_pause_only and (args.returned_pause_only or args.returned_pause_bottom_only or args.returned_pause_case is not None or args.cancel_boundary_only):parser.error('exclusive ordinary pause selection')
if args.ordinary_pause_only:args.returned_pause_bottom_only=True;args.returned_pause_only=True
if args.returned_pause_bottom_only:args.returned_pause_only=True
if args.returned_pause_only and args.cancel_boundary_only:parser.error('exclusive consumer selections')
if args.returned_pause_case is not None and not args.returned_pause_only:parser.error('returned-pause-case requires returned-pause-only')
if args.cancel_boundary_case is not None and not args.cancel_boundary_only:parser.error('cancel-boundary-case requires cancel-boundary-only')
source = args.source.read_text(encoding='utf-8-sig')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)

# Match exactly one whole definition; comments and strings retain byte offsets.
def mask(text):
    return re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                  lambda m: ' ' * len(m.group()), text, flags=re.S)

extracted = []
def function(name, text=source, origin='native/main.cpp'):
    masked = mask(text)
    matches = list(re.finditer(r'\bstatic\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*(?:noexcept(?:\s*\([^;{}]*?\))?\s*)?\{', masked))
    assert len(matches) == 1, f'native function must be unique: {origin}:{name}'
    opening = masked.index('{', matches[0].start())
    depth, end = 1, opening + 1
    while depth and end < len(masked):
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    assert depth == 0, f'unclosed native function: {origin}:{name}'
    value = text[matches[0].start():end]
    extracted.append({'origin': origin, 'name': name, 'sha256': hashlib.sha256(value.encode('utf-8')).hexdigest(), 'verbatim': True})
    return value

def section(start, end):
    assert source.count(start) == 1, f'section start must be unique: {start}'
    begin = source.index(start)
    finish = source.index(end, begin + len(start))
    return source[begin:finish]

if args.cancel_boundary_only:
    import runpy
    raise SystemExit(runpy.run_path(str(Path(__file__).parent/'fixtures/sleep_non_user_wake/abort_cancel.py'))['run'](globals()))

pause = function('sgPauseSleepTarget')
assert 'sgSleepMustNotPauseGame' not in source
assert 'sgShouldKeepInputDuringStandby' not in pause
assert 'g_sgPauseResume' in pause
assert 'std::lock_guard<std::mutex> opLock(g_sgOpMtx)' in pause
assert 'reason=manual-lease' in pause
assert 'existing.powerGeneration == generation' in pause
single_pid = function('sgSuspendGameByPidUnlocked')
assert 'const std::vector<DWORD> targets{rootPid};' in single_pid
assert 'nativeProcessCreatedFromHandle(h) == valveTarget.processCreated' in single_pid
# The NT helper remains source-only: the existing execution terminal is a
# deterministic memory function. Never label injected invocation fields as a
# runtime test of the bottom-level NT implementation or the Windows API.
for field in ['ntApiAttempted', 'ntStatus', 'processOpenError', 'identityMatches',
              'existingMarkerReused', 'pauseReason', 'apiDispatchTickMs', 'apiReturnTickMs']:
    assert '"'+field+'"' in single_pid, f'NT diagnostic source contract missing: {field}'
receipt_helper = function('sgRecordSleepPauseReceipt')
assert 'noexcept' in receipt_helper
assert receipt_helper.index('if (!sensorDetailLoggingGate()) return;') < receipt_helper.index('OpenProcess(')
assert receipt_helper.count('WakeGameResume::queryThreads(candidate.pid)') == 1
assert 'sleep-game-pause-receipt' in receipt_helper
worker_pause = section('            if (item.kind == SgWork::Suspend)', '} else if (item.kind == SgWork::WakeAutomatic)')
worker_pause_gate = worker_pause[worker_pause.index('                const auto dispatchPhase ='):worker_pause.index('// The cycle remains active')]

assert 'sgShouldKeepInputDuringStandby' not in worker_pause
assert 'const SgSuspendResult result = sgPauseSleepTarget(item.generation, (item.pauseEntryFileTime || item.pauseOrdinaryWitness) ? &item : nullptr);' in worker_pause
assert worker_pause.index('sgPrepareWakeRecoveryForSleep(item.generation') < worker_pause.index('if (g_guardEnabled && g_sgRepairEligible')
assert 'sgPrepareWakeRecoveryForSleep' not in pause
assert 'g_sgInSuspend.store(true)' not in worker_pause

assert 'modern-standby-work-loop' not in source
assert 'modern-standby-no-ntsuspend' not in source
wake_auto = section('else if (w == PBT_APMRESUMEAUTOMATIC)', 'else if (w == PBT_APMRESUMESUSPEND)')
assert 'SgWork::WakeAutomatic, "resume_auto_handheld"' in wake_auto
assert 'sgClearUnexpectedWake' not in wake_auto
assert 'sgClearInternalSleepRequest' not in wake_auto
helper = function('sgHandheldResumeInputOnWake')
assert 'sgResumeMarkedDirectoryUnlocked' not in helper
assert 'g_sgModernWakeClassified =' not in helper
assert 'g_sgModernStandbyActive =' not in helper
assert 'sgShouldKeepInputDuringStandby' not in function('sgRealWake')
assert 'nativeProcessCreatedFromHandle(process) == target.processCreated' in function('sgResumeSleepTargetUnlocked')
abort = function('sgAbortSleepIntent')
assert abort.index('std::unique_lock<std::mutex> opLock(g_sgOpMtx)') < abort.index('sgReadSleepTarget')
assert 'const auto leaseGeneration = hadOwnedMarker ? target.powerGeneration : generation;' in abort
assert 'sgResumeSleepTargetUnlocked(leaseGeneration, true, 0, abortCurrent, &retainedMarkerText)' in abort
assert 'sgQueueWork(SgWork::WakeSuspend, generation)' in function('sgFinishSleepRetryFailure')
# Input/virtual-device safeguards still use the independent platform predicate.
for token in ['modern-standby-keep-virtual', 'hidhide.unplugged-unhide-held',
              'handheld-builtin-pad-while-virtual', 'suspend-keep-writer']:
    assert token in source, f'input safeguard removed: {token}'
print('PASS source contracts: single PID, both gates removed, manual/identity/input safeguards retained', flush=True)

# Use the real registered family enum, not one invented 'handheld' value.
family_begin = source.index('enum class YmccFamily : int {')
family_end = source.index('};', family_begin) + 2
family_enum = source[family_begin:family_end]
family_names = re.findall(r'^\s*(\w+)\s*(?:=\s*\d+)?\s*(?:,|$)',
                          re.sub(r'//[^\n]*', '', family_enum.split('{', 1)[1].rsplit('}', 1)[0]), re.M)
assert family_names[0] == 'Unknown' and len(family_names) > 10

preamble = r'''
#include <atomic>
#include <cassert>
#include <cstdint>
#include <chrono>
#include <functional>
#include <thread>
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <system_error>
#include <new>
#include "fan_physical_wake.h"
#include "sleep_cancel_ready.h"
#include "sleep_power_observation.h"
namespace ymcc::inputHost::sharedPower { class Scope; }
#include "sleep_timeout_policy.h"
#include <memory>
#include <stdexcept>
#include "sleep_request_fallback.h"
#include <utility>
#include <vector>
#include <deque>
#include <condition_variable>
#include "json.hpp"
using json = nlohmann::json;
using DWORD = unsigned long; using ULONGLONG = unsigned long long;
using HANDLE = void*; using WPARAM = unsigned long long;
constexpr int WM_POWER_RESUME_READY=8;
unsigned long long g_resumeWatchdogGeneration=0;
std::vector<unsigned long long> readyMessages;
bool PostMessageW(int,int,WPARAM generation,int){readyMessages.push_back(generation);return true;}
constexpr bool FALSE = false;
constexpr int PROCESS_SUSPEND_RESUME = 1, PROCESS_QUERY_LIMITED_INFORMATION = 2;
constexpr DWORD GENERIC_READ=0x80000000ul, FILE_SHARE_READ=1, FILE_SHARE_WRITE=2, FILE_SHARE_DELETE=4;
constexpr DWORD OPEN_EXISTING=3, FILE_ATTRIBUTE_NORMAL=128, STILL_ACTIVE=259, ERROR_INVALID_PARAMETER=87;
const HANDLE INVALID_HANDLE_VALUE=reinterpret_cast<HANDLE>(~uintptr_t(0));
enum class PowerLifecycle { Ready, Suspending, Suspended, Resuming };
enum class YmccFamily { Unknown, Handheld };
enum class SgSleepMode { Unknown, S3, S4 };
enum class SgRetryKind { None, EntryFailure, UnexpectedWake, SleepTimeout };
static void sgCancelEntryFailureRetry(const char*,SgRetryKind=SgRetryKind::EntryFailure,bool=false);
static void sgSleepTimeoutStop(const char*) noexcept {}
const char* sgRetryKindName(SgRetryKind kind){return kind==SgRetryKind::EntryFailure?"entry-failure":kind==SgRetryKind::UnexpectedWake?"unexpected-wake":kind==SgRetryKind::SleepTimeout?"sleep-timeout":"none";}
enum class SgWork { Suspend, WakeAutomatic, WakeSuspend, WakeHibernate };
struct SgWorkItem {
    SgWork kind;
    unsigned long long generation;
    // One resource continuation after an issued resume, not a sleep action.
    ULONGLONG pauseEntryFileTime=0,pauseWakeFileTime=0;
    DWORD pausePid=0;
    ULONGLONG pauseProcessCreated=0;
    bool pauseOrdinaryWitness=false;
    ULONGLONG pauseTriggerFileTime=0,pauseResumeSequence=0;
};
std::deque<SgWorkItem> g_sgWorkQ;
static std::atomic<uint64_t> g_sharedPowerSuspendSerial{0};
std::condition_variable g_sgWorkCv;
bool g_sgWorkStop=false;
unsigned long long g_sgWorkWakeRevision=0;
struct NativeDetectedGame { DWORD pid=100; ULONGLONG processCreated=1234; uint64_t workingSet=768ULL*1024*1024; };
struct SgSleepTarget { DWORD pid=0; ULONGLONG processCreated=0, valveGeneration=0, powerGeneration=0; bool captured=false, markerOwned=false; };
struct SgSuspendResult { DWORD pid=0; uint64_t ws=0; bool frozen=false; };
struct SgResumeResult { int count=0; std::string names; std::vector<DWORD> pids; };
struct Task { ULONGLONG generation=7; SgSleepMode mode=SgSleepMode::S3; SgRetryKind retryKind=SgRetryKind::None; unsigned retryAttempt=0; ULONGLONG retryEntryFileTime=0; bool unexpectedWakeConsumed=false; } g_sgTask;
struct { YmccFamily family=YmccFamily::Handheld; } g_machineIdentity;
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Suspending};
std::atomic<bool> g_hardwareWriteGate{true};
std::atomic<unsigned> g_hardwareWriteInFlight{0};
struct { bool isolated=false; } g_aiFanMockSession;
std::atomic<bool> g_sensorSuspendCallbackActive{false};
std::atomic<ULONGLONG> g_sgLastKernel506EventFileTime{0},g_sgLastKernel507EventFileTime{0};
std::atomic<ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget{ymcc::sleeprequest::Mode::None};
struct {SgRetryKind kind=SgRetryKind::None;ULONGLONG dispatchFileTime=0;} g_sgInternalSleepRequest;
std::atomic<ULONGLONG> g_powerGeneration{7}, g_resumeReadyGeneration{0}, g_sgPauseWorkCompletedGeneration{0};
std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{0},g_sgLatestSleepEntryFileTime{0},g_sgOriginalPowerButtonWakeFileTime{0};
ymcc::sleeppower::CancellationReady g_sgCancellationReady;
// Existing portable test has no ingress producer; the optional cancellation
// group below executes the actual Native Dispatch and source callback.
struct CancelReceiptPort {ULONGLONG sequence=0,fileTime100ns=0;explicit operator bool()const{return sequence!=0;}};
extern "C" void returnedPublicReset();
extern "C" void returnedPublicPublish(unsigned long type,unsigned long long time,bool posted);
extern "C" void returnedPublicRead(unsigned long long* sequence,unsigned long long* time);
struct CancelDispatchPort {CancelReceiptPort resume;CancelReceiptPort latestSleepBoundary()const{return {};}CancelReceiptPort latestResumeBoundary()const{
#ifdef YMCC_RETURNED_PUBLIC_DISPATCH
 CancelReceiptPort value;returnedPublicRead(&value.sequence,&value.fileTime100ns);return value;
#else
 return resume;
#endif
}bool pendingSuspend()const{return false;}ULONGLONG observedSleepBoundarySequence()const{return 0;}} g_sleepPowerDispatch;
ULONGLONG sgNowFileTime(){return 1000000000ULL;}
std::atomic<bool> g_sgInSuspend{false}, g_sgGameActuallySuspended{false}, g_sgSleepCycleActive{true};
std::atomic<bool> g_inputReady{true}, g_inputHostPowerAdmissionRevoked{false}, g_wakeHoldPhysicalUnhide{false};
std::atomic<bool> g_exitRequested{false}, g_sgWakeRecoveryPending{false};
struct RecoveryEntry {std::wstring dir;DWORD pid=0;ULONGLONG created=0,sleepOwnerGeneration=0;std::string markerText;bool done=false;};
struct { ULONGLONG generation=0;std::vector<RecoveryEntry> entries; } g_sgWakeRecovery;
bool g_guardEnabled=true, g_sgPauseResume=true, g_sgRepairEligible=true, g_sgRetryInProgress=false;
bool g_sgModernStandbyActive=true, g_sgModernWakeClassified=false, g_sgSleepIntentArmed=false;
bool g_sgSleepQueryGateClosed=false, g_sgSleepQueryOwnsGeneration=false, g_sgPowerButtonSleepConfigured=true;
bool g_sgRetryEntryFailure=true, g_sgRetrySleepTimeout=false;
ULONGLONG g_sgModernWakeClassifiedTick=0, g_sgModernStandbyGeneration=7, g_sgLastModernStandbyIntentTick=0;
ULONGLONG g_sgSleepQueryGeneration=0, g_sgRetryDueTick=0, g_lastResumeNotifyTick=0, g_sgSleepTriggerTick=0;
double g_sgSleepTriggerEpoch=0;
int g_sgLastS0WakeReason=1, g_hwnd=1;
constexpr int SG_RETRY_TIMER_ID=1;
constexpr ULONGLONG SG_SLEEP_RESUME_RETRY_DELAYS_MS[]={0,100,300,600};
const std::wstring SG_DIR=L"sleep", SG_SLEEP_LEASE_DIR=L"sleep/suspended", SG_MANUAL_DIR=L"sleep/manual";
std::mutex g_sgOpMtx, g_sgSleepTargetMx;
SgSleepTarget g_sgSleepTarget;
NativeDetectedGame detected;
bool aoac=false, targetAvailable=true, manualLease=false, markerExists=false, resumeFails=false, swapOnOpen=false, processAlive=true;
bool manualMarkerIoFailed=false, markerIoFailed=false;
ULONGLONG actualCreated=1234, markerCreated=1234, markerGeneration=7, nowTick=10000;
std::string markerReadBody;size_t markerReadOffset=0;
int freezeDepth=0, suspendCalls=0, resumeCalls=0, powerReleaseCalls=0, resetCalls=0, sensorCalls=0, rescanCalls=0, abortCalls=0;
int focusCalls=0,recoveryCancelCalls=0,ntResumeAttempts=0;
std::vector<std::pair<SgWork,ULONGLONG>> queue;
std::function<void()> pauseHook;
unsigned pauseMetadataCalls=0;
std::function<void()> pauseMetadataHook;
std::function<void()> resumeDelayHook,resumeOpenHook,resumeDrainHook,resumeMarkerReadHook;
// Explicit detail/probe ports for the extracted pause receipt. All state is
// memory-only; the single read counts calls and can revoke context mid-probe.
bool pauseDetailLogging=false;
unsigned pauseThreadQueries=0;
std::function<void()> pauseThreadProbeHook;
json pauseInvocationOverride=json::object();
namespace WakeGameResume {
struct ThreadState {
 bool known=false;unsigned total=0,suspended=0;
 bool allSuspended()const{return known&&total&&suspended==total;}
 bool allRunning()const{return known&&total&&suspended==0;}
};
ThreadState pauseThreadState{true,3,3};
ThreadState queryThreads(DWORD pid){
 assert(pid==100);++pauseThreadQueries;
 if(pauseThreadProbeHook)pauseThreadProbeHook();
 return pauseThreadState;
}
}
bool sensorDetailLoggingGate(){return pauseDetailLogging;}
std::atomic<bool> cancelEntered{false};
std::vector<std::pair<std::string,json>> facts;
ULONGLONG currentPowerGeneration(){return g_powerGeneration.load();}
ULONGLONG GetTickCount64(){return nowTick;}
void Sleep(DWORD){if(resumeDelayHook){auto hook=std::exchange(resumeDelayHook,{});hook();}}
void KillTimer(int,int){}
const char* powerLifecycleName(PowerLifecycle){return "test";}
template<class... T> void traceLog(const char*,T...){}
void sgRecordFact(const char* event,const json& details=json::object()){facts.emplace_back(event,details);}
void appendNativeLifecycleLog(const char*,const json&){}
void appendFanLifecycleLog(const char*,const json&){}
void fanHostScheduleEmergencyResume(ULONGLONG){}
void deckySidebarResumeAfterIntent(ULONGLONG){} // transport memory port; Decky recovery is separately tested
std::vector<std::pair<std::string,json>> emittedEvents;
void ipc_emit(const char* name,const json& details){emittedEvents.emplace_back(name,details);}
bool sgPlatformIsAlwaysOnAlwaysConnected(){return aoac;}
// This PID-only fixture supplies S0 intent explicitly; capability routing is
// tested with real production handlers in the S3/non-user companion replay.
bool sgModernStandbyRepairSupported(){return true;}
bool nativeValveReadCurrentTarget(NativeDetectedGame* target,ULONGLONG* generation){if(!targetAvailable)return false;*target=detected;*generation=11;return true;}
bool nativeValveValidateOwnedSuspendedTarget(const std::wstring& dir,DWORD pid,ULONGLONG created){
 if(dir==SG_MANUAL_DIR)return manualLease;
 return markerExists && pid==100 && created==markerCreated && created==actualCreated;
}
bool sgReadSleepTarget(SgSleepTarget& target){target=g_sgSleepTarget; return target.captured;}
void sgClearSleepTarget(){g_sgSleepTarget={};}
void sgClearSleepTargetIfMatches(const SgSleepTarget& target){
 if(g_sgSleepTarget.pid==target.pid && g_sgSleepTarget.processCreated==target.processCreated && g_sgSleepTarget.powerGeneration==target.powerGeneration)sgClearSleepTarget();
}
bool sgSleepLeaseOwnedByGeneration(ULONGLONG generation){return g_sgSleepTarget.markerOwned && g_sgSleepTarget.powerGeneration==generation;}
ULONGLONG sgMarkerGeneration(const std::wstring&,DWORD){return markerGeneration;}
bool sgHasMarkerFiles(const std::wstring&){return markerExists;}
void sgWriteFile(const std::wstring& path,const std::string&){
 if(path!=SG_DIR+L"\\target.txt")return;
 ++pauseMetadataCalls;
 if(pauseMetadataHook){auto hook=std::exchange(pauseMetadataHook,{});hook();}
}
double sgNowEpoch(){return 1;}
void sgInitNt(){}
bool focusQueryProcessIdentity(DWORD,void*,ULONGLONG* created){*created=actualCreated; return processAlive;}
HANDLE OpenProcess(int,bool,DWORD pid){if(resumeOpenHook){auto hook=std::exchange(resumeOpenHook,{});hook();}if(swapOnOpen){actualCreated++;swapOnOpen=false;}return processAlive?reinterpret_cast<HANDLE>(static_cast<uintptr_t>(pid)):nullptr;}
ULONGLONG nativeProcessCreatedFromHandle(HANDLE){return actualCreated;}
void CloseHandle(HANDLE){}
DWORD GetCurrentProcessId(){return 901;}
DWORD GetLastError(){return processAlive?5:ERROR_INVALID_PARAMETER;}
bool ProcessIdToSessionId(DWORD,DWORD* session){*session=1;return true;}
bool GetExitCodeProcess(HANDLE,DWORD* status){*status=processAlive?STILL_ACTIVE:0;return true;}
// Typed marker ports use a separate immutable birth captured by the fake suspend.
// They touch no user file: reads are the explicit memory marker of this PID.
HANDLE CreateFileW(const wchar_t* path,DWORD access,DWORD share,void*,DWORD creation,DWORD flags,void*){
 if(!markerExists||std::wstring(path)!=SG_SLEEP_LEASE_DIR+L"\\100.txt"||access!=GENERIC_READ||
    share!=(FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE)||creation!=OPEN_EXISTING||flags!=FILE_ATTRIBUTE_NORMAL)
   return INVALID_HANDLE_VALUE;
 markerReadBody="pid=100|created="+std::to_string(markerCreated)+"|generation="+std::to_string(markerGeneration)+"|state=suspended";
 markerReadOffset=0;return reinterpret_cast<HANDLE>(900);
}
bool ReadFile(HANDLE file,void* buffer,DWORD bytes,DWORD* count,void*){
 if(markerIoFailed)return false;
 if(file!=reinterpret_cast<HANDLE>(900)||!count)return false;
 const size_t available=markerReadBody.size()-markerReadOffset;
 *count=static_cast<DWORD>(std::min<size_t>(bytes,available));
 markerReadBody.copy(static_cast<char*>(buffer),*count,markerReadOffset);markerReadOffset+=*count;
 if(*count&&resumeMarkerReadHook){auto hook=std::exchange(resumeMarkerReadHook,{});hook();}return true;
}
long resumeProcess(HANDLE handle){assert(handle==reinterpret_cast<HANDLE>(100));++ntResumeAttempts;if(resumeFails||freezeDepth==0)return -1;++resumeCalls;--freezeDepth;if(resumeDrainHook){auto hook=std::exchange(resumeDrainHook,{});hook();}return 0;}
auto fnNtResume=resumeProcess;
void focusSingleResumedGame(const std::vector<DWORD>&,ULONGLONG,const ymcc::sleeppower::SharedPowerV1&){++focusCalls;}
namespace fspath {
 bool exists(const std::wstring& path,std::error_code& error){
  if(path==SG_MANUAL_DIR+L"\\100.txt"){
   if(manualMarkerIoFailed)error=std::make_error_code(std::errc::permission_denied);
   return manualLease;
  }
  return path==SG_SLEEP_LEASE_DIR+L"\\100.txt"&&markerExists;
 }
 void remove(const std::wstring& path,std::error_code&){assert(path==SG_SLEEP_LEASE_DIR+L"\\100.txt");markerExists=false;}
}
SgResumeResult sgResumeMarkedDirectoryUnlocked(const std::wstring& dir,bool){assert(dir==SG_SLEEP_LEASE_DIR); if(markerExists && !resumeFails){markerExists=false;freezeDepth=0;}return {};}
std::function<void()> pauseFinalHook;
json sgSuspendGameByPidUnlocked(DWORD pid,const std::wstring& dir,ULONGLONG created,const NativeDetectedGame*,bool,ULONGLONG gen,const std::function<bool()>& requestCurrent=std::function<bool()>()){
 if(pauseFinalHook){auto hook=std::exchange(pauseFinalHook,{});hook();}
 if(requestCurrent&&!requestCurrent())return {{"paused",false},{"pauseReason","request-revoked"}};
 assert(pid==100 && dir==SG_SLEEP_LEASE_DIR && created==actualCreated);
 ++suspendCalls;
 json invocation={{"paused",true},{"okCount",1},{"failCount",0},
  {"ntApiAttempted",true},{"ntStatus",0},{"identityMatches",true},
  {"processOpenError",0},{"existingMarkerReused",false},{"pauseReason","nt-suspend-accepted"},
  {"apiDispatchTickMs",nowTick},{"apiReturnTickMs",nowTick}};
 for(auto it=pauseInvocationOverride.begin();it!=pauseInvocationOverride.end();++it)invocation[it.key()]=it.value();
 if(invocation.value("paused",false)){
  ++freezeDepth;markerExists=true;markerCreated=created;markerGeneration=gen;
 }
 if(pauseHook)pauseHook();
 return invocation;
}
void sgStopSleepRetry(bool=false){g_sgRetryInProgress=false;g_sgRetryDueTick=0;g_sgSleepTriggerTick=0;g_sgTask.retryKind=SgRetryKind::None;g_sgTask.retryAttempt=0;cancelEntered=true;}
bool sgSleepRetryActive(){return g_sgRetryInProgress && g_sgTask.retryKind!=SgRetryKind::None;}
// This harness isolates PID ownership; the button-scoped wake policy has its
// own extracted-production replay in sleep_non_user_wake_selftest.py.
bool sgNonUserWakeGuardEnabled(){return false;}
bool sgExternalDeviceWakeIntentAge(ULONGLONG& age){age=0;return false;}
void stopPowerResumeWatchdog(){}
void armPowerResumeWatchdog(ULONGLONG){}
void sgClearInternalSleepRequest(const char*){}
void hidHideInvalidateRawInputIdentityCache(){}
void xinputSlotsMarkDiscoveryDirty(){}
void openHardwareWriteGate(){g_hardwareWriteGate=true;}
void closeHardwareWriteGate(const char*){g_hardwareWriteGate=false;}
void inputCaptureResumeSensorsForPower(bool){++sensorCalls;}
void inputHostRequestWakeRescan(ULONGLONG){++rescanCalls;}
void inputHostRequestPowerRelease(ULONGLONG,const char*){++powerReleaseCalls;}
void gamepadResetNativeState(bool){++resetCalls;}
void sgHandheldSleepSensorsOnly(const char*){}
void sgStopMonitorEntryFailureTimer(const char*){}
void fanHostScheduleEmergencySuspend(const char*,ULONGLONG,ULONGLONG=0){}
void sleepRecoveryAutoCancelPreparedSuspend(){}
bool sgPowerButtonSleepConfigured(){return true;}
bool sgHibernateAvailable(){return true;}
void sgQueueWork(SgWork work,ULONGLONG gen,ULONGLONG=0){queue.emplace_back(work,gen);}
void sgMarkSleepTrigger(){g_sgTask.generation=currentPowerGeneration();g_sgRepairEligible=g_guardEnabled&&g_sgSleepIntentArmed;g_sgSleepCycleActive=g_sgRepairEligible;g_sgGameActuallySuspended=false;g_sgSleepIntentArmed=false;}
void sgClearUserStandby(const char*){++abortCalls;}
void inputHostRequestPowerRearm(ULONGLONG,const char*){}
bool sgS0EntryFailureEligible(int reason,ULONGLONG){return reason==7 || reason==8;}
bool sgKernelPowerWakePredatesLatestEntry(int,ULONGLONG,ULONGLONG*){return false;}
void sgObserveUnattributedSleepRetryFailure(const char*,int,ULONGLONG){}
void sgStartSleepRetry(SgRetryKind kind,const char*){g_sgTask.retryKind=kind;g_sgRetryInProgress=true;}
'''
names = ['initialPowerGeneration','hardwareWriteAllowed','beginHardwareWriteLease','endHardwareWriteLease',
         'powerLifecycleMatches','publishPowerResumeReady','reconcilePowerResumeReady',
         'sgShouldKeepInputDuringStandby','sgVirtualWriterAllowedNow',
         'sgReadResumeMarkerChecked','sgResumeMarkerDefinitelyStale',
         'sgResumeSleepTargetUnlocked','sgResumeSleepTarget','sgRecordManualGameControlReceipt','sgRecordSleepPauseReceipt','sgPauseSleepTarget',
         'sgRealWake','sgAbortSleepIntent','sgCancelEntryFailureRetry','sgHandheldResumeInputOnWake','handlePowerResumeNotification',
         'sgBeginModernStandbyIntent','sgHandleModernStandbyWake']
if re.search(r'static SgResumeResult sgResumeHibernateSleepTarget\(', source):
    names.insert(names.index('sgRealWake'), 'sgResumeHibernateSleepTarget')
import runpy
current_power_support=Path(__file__).parent/'fixtures/sleep_non_user_wake/pause_current_power_ports.py'
preamble+=runpy.run_path(str(current_power_support))['bind'](globals())
returned_source=(args.source.resolve().parent/'sleep_returned_pause_native.inc').read_text(encoding='utf-8-sig')
preamble,returned_functions=runpy.run_path(str(Path(__file__).parent/'fixtures/sleep_non_user_wake/retained_delivery.py'))['bind_pause'](globals(),returned_source)
functions=returned_functions+'\n'+'\n\n'.join(function(name) for name in names)
drain_functions = function('sgDrainProcessSuspendCount')
if re.search(r'static bool sgDrainProcessSuspendCountIfCurrent\(',source):
    drain_functions = 'template<class Current>\n' + function('sgDrainProcessSuspendCountIfCurrent') + '\n' + drain_functions
functions = drain_functions + '\n' + functions
# These added contexts execute only memory state. The OEM production scheduler
# remains outside this PID fixture; the port records generation/reason/order.
pure_context = r'''
ymcc::fan_wake::Observation g_fanPhysicalWake;
struct OemWakePortCall { unsigned long long generation;std::string reason;int sensorsBefore; };
std::vector<OemWakePortCall> oemWakePortCalls;
void oemVendorStackShieldWakeBegin(unsigned long long generation,const char* reason){
 oemWakePortCalls.push_back({generation,reason?reason:"",sensorCalls});
}
namespace SleepSteamRecovery {
std::mutex captureMx;
unsigned long long repeatedSuspendGeneration=0,repeatedSuspendEpoch=0;
'''
steam_source_path = args.source.resolve().parent/'sleep_steam_recovery.h'
auto_source_path = args.source.resolve().parent/'sleep_recovery_auto.h'
steam_source = steam_source_path.read_text(encoding='utf-8-sig')
auto_source = auto_source_path.read_text(encoding='utf-8-sig')
pure_context += function('closeSuspendEpisode',steam_source,'native/sleep_steam_recovery.h') + '\n}\n'
pure_context += function('sleepRecoveryAutoCloseSuspendEpisode',auto_source,'native/sleep_recovery_auto.h') + '\n'
pure_context += function('currentFanPowerGeneration') + '\n'
wake_recovery_source = (args.source.resolve().parent/'wake_game_recovery.h').read_text(encoding='utf-8-sig')
pure_context += function('sgReconcileSleepGameFlagsAfterRecoveryCancel',wake_recovery_source,'native/wake_game_recovery.h') + '\n'
preamble += pure_context
# The real definition inherits this default argument from its forward declaration.
preamble += '\nstatic SgResumeResult sgResumeSleepTarget(unsigned long long, bool = false);\n'
preamble += '''
int queuedGameRecovery=0;
void msiVisibilityObserverNoteResume(unsigned long long){}
void radialInputIsolationInvalidate(){}
static struct {bool active() const {return false;} void shutdown(){}} g_radialOverlay;
void inputHostArmWakePhysicalHold(const char*){}
static void sleepRecoveryAutoArm(unsigned long long){} // automatic owner separately tested; this fixture keeps opt-in off
static bool sgBeginWakeGameRecovery(unsigned long long generation,const SgWorkItem* =nullptr){assert(generation==7);++queuedGameRecovery;return true;}
static void sgCancelWakeGameRecoveryUnlocked(const char* reason){++recoveryCancelCalls;g_sgWakeRecoveryPending=false;g_sgWakeRecovery={};sgReconcileSleepGameFlagsAfterRecoveryCancel(reason);}
static void sgApplyLongSleepResumeGrace(unsigned long long){}
static bool sgPrepareWakeRecoveryForSleep(unsigned long long,
 const std::function<bool()>& current=std::function<bool()>()){
 if(current&&!current())return false;
 // Boundary fake: production reconciliation is compiled in the wake-header suite.
 // Here it exposes dispatch ordering, including flags written before capture.
 const bool owned=g_sgSleepTarget.markerOwned || markerExists;
 g_sgInSuspend=owned;g_sgGameActuallySuspended=owned;return !current||current();
}
'''

checks = r'''
void reset(){
 g_powerLifecycle=PowerLifecycle::Suspending;g_powerGeneration=7;g_resumeReadyGeneration=0;
 g_exitRequested=false;g_sgWakeRecoveryPending=false;g_sgWakeRecovery={};
 g_hardwareWriteGate=true;g_hardwareWriteInFlight=0;g_aiFanMockSession.isolated=false;g_resumeWatchdogGeneration=0;readyMessages.clear();
 g_sensorSuspendCallbackActive=false;g_sgLastKernel506EventFileTime=0;g_sgLastKernel507EventFileTime=0;g_sgInternalSleepRequest={};g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::None;
 g_sgTask={};g_sgInSuspend=false;g_sgGameActuallySuspended=false;g_sgSleepCycleActive=true;
 g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_wakeHoldPhysicalUnhide=false;
 g_guardEnabled=true;g_sgPauseResume=true;g_sgRepairEligible=true;g_sgRetryInProgress=false;
 g_sgModernStandbyActive=true;g_sgModernWakeClassified=false;g_sgModernWakeClassifiedTick=0;
 g_sgLastModernStandbyIntentTick=0;g_lastResumeNotifyTick=0;g_sgSleepIntentArmed=false;
 g_sgSleepQueryGateClosed=false;g_sgSleepQueryOwnsGeneration=false;g_sgSleepQueryGeneration=0;
 g_machineIdentity.family=YmccFamily::Handheld;aoac=false;targetAvailable=true;manualLease=false;
 markerExists=false;resumeFails=false;swapOnOpen=false;processAlive=true;actualCreated=1234;markerCreated=1234;markerReadBody.clear();markerReadOffset=0;
 manualMarkerIoFailed=false;markerIoFailed=false;focusCalls=0;recoveryCancelCalls=0;ntResumeAttempts=0;resumeDelayHook={};resumeOpenHook={};resumeDrainHook={};resumeMarkerReadHook={};
 markerGeneration=7;nowTick=10000;g_sgSleepTarget={};detected={};queue.clear();facts.clear();pauseHook={};cancelEntered=false;
 emittedEvents.clear();oemWakePortCalls.clear();
 pauseDetailLogging=false;pauseThreadQueries=0;pauseThreadProbeHook={};
 pauseMetadataCalls=0;pauseMetadataHook={};
 pauseInvocationOverride=json::object();WakeGameResume::pauseThreadState={true,3,3};
 g_fanPhysicalWake.~Observation();new (&g_fanPhysicalWake) ymcc::fan_wake::Observation();
 SleepSteamRecovery::repeatedSuspendGeneration=0;SleepSteamRecovery::repeatedSuspendEpoch=0;
 queuedGameRecovery=0;freezeDepth=suspendCalls=resumeCalls=powerReleaseCalls=resetCalls=sensorCalls=rescanCalls=abortCalls=0;
}
void freeze(){auto r=sgPauseSleepTarget(currentPowerGeneration());assert(r.frozen&&r.pid==100);g_sgInSuspend=true;g_sgGameActuallySuspended=true;}
int main(){
 reset();freeze();g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgRetryInProgress=true;g_sgRetryDueTick=10500;
 sgCancelEntryFailureRetry("setting-off");assert(!g_sgRetryInProgress&&freezeDepth==0&&resumeCalls==1&&g_powerLifecycle==PowerLifecycle::Ready);
 reset();freeze();g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgRetryInProgress=true;g_powerLifecycle=PowerLifecycle::Suspended;
 sgCancelEntryFailureRetry("setting-off");assert(!g_sgRetryInProgress&&freezeDepth==1&&resumeCalls==0&&g_sgSleepTarget.markerOwned&&g_powerLifecycle==PowerLifecycle::Suspended);
 reset();freeze();g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgRetryInProgress=true;g_sensorSuspendCallbackActive=true;
 sgCancelEntryFailureRetry("setting-off");assert(freezeDepth==1&&resumeCalls==0&&g_sensorSuspendCallbackActive);
 reset();freeze();g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgRetryInProgress=true;g_sgLastKernel506EventFileTime=200;g_sgLastKernel507EventFileTime=100;
 sgCancelEntryFailureRetry("setting-off");assert(freezeDepth==1&&resumeCalls==0);
 reset();freeze();g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgRetryInProgress=true;g_sgRetryDueTick=0;
 g_sgInternalSleepRequest={SgRetryKind::EntryFailure,100};g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::ModernStandby;
 sgCancelEntryFailureRetry("setting-off");assert(freezeDepth==1&&resumeCalls==0&&g_sgInternalSleepRequest.kind==SgRetryKind::EntryFailure);
 reset();freeze();g_sgTask.retryKind=SgRetryKind::UnexpectedWake;g_sgRetryInProgress=true;
 sgCancelEntryFailureRetry("setting-off");assert(g_sgRetryInProgress&&freezeDepth==1&&g_sgTask.retryKind==SgRetryKind::UnexpectedWake);
 reset();freeze();actualCreated++;g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgRetryInProgress=true;
 sgCancelEntryFailureRetry("setting-off");assert(resumeCalls==0);
 reset();
 int passed=0;
 auto pass=[&](const char* name){++passed;std::cout<<"PASS "<<name<<"\n";};
 for(bool known:{false,true})for(bool caps:{false,true})for(bool enabled:{false,true}){
  reset();g_machineIdentity.family=known?YmccFamily::Handheld:YmccFamily::Unknown;aoac=caps;g_sgPauseResume=enabled;
  assert(sgShouldKeepInputDuringStandby()==(known||caps));auto r=sgPauseSleepTarget(7);
  assert(r.frozen==enabled&&suspendCalls==int(enabled));assert(powerReleaseCalls==0&&resetCalls==0);
 }
 pass("8 platform/capability/option combinations: PID pause independent of input policy");
 reset();freeze();auto duplicate=sgPauseSleepTarget(7);assert(duplicate.frozen&&suspendCalls==1&&freezeDepth==1);
 pass("same lease cannot increment the suspend count twice");
 for(bool paused:{false,true}){
  reset();g_sgModernStandbyActive=false;if(paused)freeze();
  sgBeginModernStandbyIntent();assert(currentPowerGeneration()==7&&abortCalls==0&&queue.empty());
  if(!paused)freeze();assert(suspendCalls==1&&freezeDepth==1);
 }
 pass("PBT then 506 joins original generation before or after pause worker");
 reset();g_sgModernStandbyActive=false;g_powerLifecycle=PowerLifecycle::Ready;
 sgBeginModernStandbyIntent();assert(currentPowerGeneration()==8&&queue.size()==1);freeze();sgBeginModernStandbyIntent();
 assert(suspendCalls==1&&queue.size()==1&&abortCalls==0);
 pass("506 first and duplicate 506 queue one pause transaction");
 reset();manualLease=true;assert(!sgPauseSleepTarget(7).frozen&&suspendCalls==0&&manualLease);
 pass("manual pause lease never double-frozen or stolen");
 reset();targetAvailable=false;assert(!sgPauseSleepTarget(7).frozen&&suspendCalls==0);
 pass("no admitted game means no process action");
 reset();assert(!sgPauseSleepTarget(6).frozen&&suspendCalls==0);g_powerLifecycle=PowerLifecycle::Resuming;
 assert(!sgPauseSleepTarget(7).frozen&&suspendCalls==0);
 pass("stale generation / fast wake before pause fail closed");
 reset();freeze();g_sgModernWakeClassified=false;sgHandheldResumeInputOnWake("auto");
 assert(sensorCalls==1&&freezeDepth==1&&resumeCalls==0&&g_sgModernStandbyActive&&!g_sgModernWakeClassified);
 pass("input-only wake helper cannot thaw game or classify wake");
 reset();freeze();handlePowerResumeNotification(SgWork::WakeAutomatic,"resume_auto_handheld");
 assert(g_inputReady&&sgVirtualWriterAllowedNow()&&powerReleaseCalls==0&&resetCalls==0);
 assert(queue.back().first==SgWork::WakeAutomatic&&queue.back().second==7);
 sgRealWake("resume_auto",7);assert(freezeDepth==1&&markerExists&&resumeCalls==0);
 g_powerLifecycle=PowerLifecycle::Ready;g_resumeReadyGeneration=7;nowTick+=60000;
 handlePowerResumeNotification(SgWork::WakeSuspend,"resume_suspend");
 assert(currentPowerGeneration()==7&&queue.back().first==SgWork::WakeSuspend&&queue.back().second==7);
 sgRealWake("resume_suspend",7);assert(queuedGameRecovery==1&&resumeCalls==0);sgResumeSleepTarget(7,false);assert(resumeCalls==1&&freezeDepth==0&&!markerExists);
 pass("background wake keeps writer and PID lease; user wake 60s later recovers same generation");
 reset();freeze();handlePowerResumeNotification(SgWork::WakeAutomatic,"auto");sgRealWake("resume_auto",7);
 g_powerLifecycle=PowerLifecycle::Ready;g_resumeReadyGeneration=7;
 for(bool user:{true,false,true}){
  nowTick+=60000;const auto events=emittedEvents.size();const auto rescans=rescanCalls;
  sgHandleModernStandbyWake(user);
  assert(g_powerLifecycle==PowerLifecycle::Ready&&currentPowerGeneration()==7&&g_resumeReadyGeneration==7);
  assert(emittedEvents.size()==events&&rescanCalls==rescans&&powerReleaseCalls==0&&resetCalls==0);
  for(const char* consumer:{"registry.writePowerBatch","power.setActiveScheme","tdpDaemon.set"}){
   assert(beginHardwareWriteLease(consumer));assert(g_hardwareWriteInFlight==1);endHardwareWriteLease();
  }
  assert(g_hardwareWriteInFlight==0&&queue.back().second==7);
 }
 assert(queue.back().first==SgWork::WakeSuspend&&queue.back().second==7);sgRealWake("resume_suspend",7);assert(queuedGameRecovery==1&&resumeCalls==0);sgResumeSleepTarget(7,false);
 assert(resumeCalls==1&&freezeDepth==0);
 // A genuine next sleep still enters Resuming and rejects both CPU/TDP;
 // duplicate classification reuses that transaction, and Ready alone cannot
 // override a closed write gate or an isolated mock session.
 for(auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended}){
  reset();g_powerGeneration=8;g_powerLifecycle=phase;g_hardwareWriteGate=false;
  sgHandleModernStandbyWake(true);
  assert(g_powerLifecycle==PowerLifecycle::Resuming&&currentPowerGeneration()==8&&g_resumeReadyGeneration==0);
  assert(!beginHardwareWriteLease("registry.writePowerBatch")&&!beginHardwareWriteLease("tdpDaemon.set")&&g_hardwareWriteInFlight==0);
  g_sgInSuspend=true;const auto events=emittedEvents.size();nowTick+=60000;sgHandleModernStandbyWake(true);
  assert(g_powerLifecycle==PowerLifecycle::Resuming&&emittedEvents.size()==events&&!hardwareWriteAllowed());
 }
 reset();g_powerLifecycle=PowerLifecycle::Ready;g_hardwareWriteGate=false;sgHandleModernStandbyWake(true);assert(!hardwareWriteAllowed());
 g_hardwareWriteGate=true;g_aiFanMockSession.isolated=true;assert(!beginHardwareWriteLease("tdpDaemon.set"));
 // A completed worker may republish after a lost Ready message; old
 // generations and genuine sleep never receive an admission signal.
 reset();g_powerLifecycle=PowerLifecycle::Resuming;g_resumeReadyGeneration=7;
 publishPowerResumeReady(7);publishPowerResumeReady(7);assert(readyMessages.size()==2&&readyMessages.back()==7);
 publishPowerResumeReady(6);assert(readyMessages.size()==2);
 for(auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Ready}){
  g_powerLifecycle=phase;publishPowerResumeReady(7);assert(readyMessages.size()==2);
 }
 g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;g_resumeReadyGeneration=0;publishPowerResumeReady(7);assert(readyMessages.size()==2&&g_resumeReadyGeneration==0);
 publishPowerResumeReady(8);assert(readyMessages.size()==3&&readyMessages.back()==8&&g_resumeReadyGeneration==8);
 reset();g_powerLifecycle=PowerLifecycle::Resuming;g_resumeWatchdogGeneration=7;
 reconcilePowerResumeReady(7);assert(readyMessages.empty()); // Timeout alone proves nothing.
 g_resumeReadyGeneration=7;g_sgRetryInProgress=true;g_sgTask.retryKind=SgRetryKind::EntryFailure;
 reconcilePowerResumeReady(7);assert(readyMessages.empty()); // Do not cancel a re-sleep owner.
 g_sgRetryInProgress=false;g_sgTask.retryKind=SgRetryKind::None;
 reconcilePowerResumeReady(7);assert(readyMessages.size()==1&&readyMessages.back()==7);
 g_powerGeneration=8;reconcilePowerResumeReady(7);reconcilePowerResumeReady(8);assert(readyMessages.size()==1);
 nowTick=10000;const auto previousProcessGeneration=initialPowerGeneration()+5;
 nowTick=20000;assert(initialPowerGeneration()>previousProcessGeneration&&initialPowerGeneration()>0);
 pass("Kernel-Power 507 user evidence releases held lease after input Ready");
 reset();freeze();g_sgRetryInProgress=true;g_sgTask.retryKind=SgRetryKind::EntryFailure;
 handlePowerResumeNotification(SgWork::WakeAutomatic,"auto");assert(queue.empty()&&freezeDepth==1&&resumeCalls==0&&sgSleepRetryActive());
 handlePowerResumeNotification(SgWork::WakeSuspend,"user");assert(!sgSleepRetryActive());sgRealWake("resume_suspend",7);assert(queuedGameRecovery==1&&resumeCalls==0);sgResumeSleepTarget(7,false);
 assert(resumeCalls==1&&freezeDepth==0);
 pass("automatic wake cannot consume retry; explicit user wake cancels and recovers");
 reset();freeze();actualCreated++;auto stale=sgResumeSleepTarget(7,false);
 assert(stale.count==0&&resumeCalls==0&&!markerExists);
 pass("reused PID cannot be resumed");
 reset();freeze();swapOnOpen=true;auto swapped=sgResumeSleepTarget(7,false);
 assert(swapped.count==0&&resumeCalls==0);
 pass("PID reuse between identity query and OpenProcess is rejected on opened handle");
 reset();freeze();g_powerGeneration=8;g_sgTask.generation=8;g_powerLifecycle=PowerLifecycle::Resuming;
 sgRealWake("resume_hibernate",8);
 assert(resumeCalls==1&&freezeDepth==0&&!markerExists&&!g_sgSleepTarget.markerOwned);
 pass("S4 final wake returns the exact retained sleep lease after a duplicate suspend changed power generation");
 reset();freeze();auto wrong=sgResumeSleepTarget(6,false);
 assert(wrong.count==0&&freezeDepth==1&&markerExists);
 pass("wrong power generation cannot resume a valid lease");
 reset();freeze();resumeFails=true;auto failed=sgResumeSleepTarget(7,false);
 assert(failed.count==0&&freezeDepth==1&&markerExists&&g_sgSleepTarget.markerOwned);
 pass("failed resume retains owned marker for recovery");
 reset();freeze();manualLease=true;sgRealWake("resume_suspend",7);assert(queuedGameRecovery==1&&resumeCalls==0);sgResumeSleepTarget(7,false);
 assert(freezeDepth==0&&resumeCalls==1&&manualLease);
 pass("explicit wake queues recovery without inline thaw; legacy exact-lease helper stays separate (manual batch covered by wake suite)");
 reset();freeze();g_sensorSuspendCallbackActive=true;sgAbortSleepIntent("user-cancel", 0);
 assert(resumeCalls==1&&freezeDepth==0&&!markerExists&&g_powerLifecycle==PowerLifecycle::Ready&&!g_sensorSuspendCallbackActive);
 pass("canceled sleep recovers only its exact lease");
 reset();std::thread cancelThread;
 pauseHook=[&]{
  cancelThread=std::thread([]{sgAbortSleepIntent("cancel-during-freeze", 0);});
  while(!cancelEntered.load())std::this_thread::yield();
  std::this_thread::sleep_for(std::chrono::milliseconds(25));
 };
 sgPauseSleepTarget(7);cancelThread.join();
 assert(resumeCalls==1&&freezeDepth==0&&!markerExists&&!g_sgSleepTarget.markerOwned);
 pass("cancel racing in-flight freeze waits for published lease and recovers it");
 std::cout<<"native sleep-game pause selftest: "<<passed<<" groups PASS (actual extracted functions; OS/input boundaries mocked)\n";
}
'''
preamble=preamble.replace('enum class YmccFamily { Unknown, Handheld };',family_enum)
preamble=preamble.replace('YmccFamily::Handheld','YmccFamily::AsusRogAllyX')
checks=checks.replace('YmccFamily::Handheld','YmccFamily::AsusRogAllyX')
previous_family_names = [name for name in family_names if name != 'ValveSteamMachine']
assert len(previous_family_names) == len(family_names) - 1
family_checks = '\n'.join('  YmccFamily::'+name+',' for name in previous_family_names)
checks=checks.replace(' std::cout<<"native sleep-game pause selftest:',r'''
 for(auto family:{
FAMILY_VALUES
 }) for(bool caps:{false,true}) for(bool enabled:{false,true}) {
  reset();g_machineIdentity.family=family;aoac=caps;g_sgPauseResume=enabled;
  assert(sgShouldKeepInputDuringStandby()==(family!=YmccFamily::Unknown||caps));
  runProductionSuspendPause(7);
  assert(suspendCalls==int(enabled)&&freezeDepth==int(enabled));
  assert(g_sgInSuspend==enabled&&g_sgGameActuallySuspended==enabled);
  assert(powerReleaseCalls==0&&resetCalls==0);
 }
 pass("all registered machine families: shared game pause and independent retained input policy");
 std::cout<<"native sleep-game pause selftest:'''.replace('FAMILY_VALUES',family_checks))
functions += '\nvoid runProductionSuspendPause(ULONGLONG generation,const SgWorkItem* scope=nullptr){\nconst SgWorkItem item=scope?*scope:SgWorkItem{SgWork::Suspend,generation};\nfor(unsigned once=0;once<1;++once){\n' + worker_pause_gate + '\n}}\n'
hibernate_dispatch = section('            } else if (item.kind == SgWork::WakeHibernate) {',
                             '            } else if (item.kind == SgWork::WakeSuspend) {')
hibernate_dispatch = hibernate_dispatch[hibernate_dispatch.index('{')+1:]
functions += '\nvoid runProductionHibernateWake(ULONGLONG generation){\nstruct { ULONGLONG generation; } item{generation};\nfor(unsigned once=0;once<1;++once){\n' + hibernate_dispatch + '\n}}\n'
checks=checks.replace(' std::cout<<"native sleep-game pause selftest:',r'''
 reset();freeze();g_powerGeneration=8;g_sgTask={};g_sgInSuspend=false;g_sgGameActuallySuspended=false;g_sgSleepCycleActive=false;g_powerLifecycle=PowerLifecycle::Resuming;
 runProductionHibernateWake(8);
 assert(resumeCalls==1&&freezeDepth==0&&!markerExists&&!g_sgSleepTarget.markerOwned&&!g_sgInSuspend&&!g_sgGameActuallySuspended&&focusCalls==0);
 runProductionHibernateWake(8);assert(resumeCalls==1&&focusCalls==0);
 pass("actual S4 worker returns retained lease even with lost flags and duplicate S4 does no further resume or focus");
 reset();freeze();g_powerGeneration=8;g_sgTask.generation=8;g_powerLifecycle=PowerLifecycle::Resuming;
 runProductionHibernateWake(7);sgRealWake("resume_hibernate",7);
 assert(resumeCalls==0&&freezeDepth==1&&markerExists&&g_sgTask.generation==8&&g_sgInSuspend);
 pass("old S4 work cannot consume current task or retained lease");
 for(bool unknown:{false,true}){
  reset();freeze();g_powerGeneration=8;g_sgTask.generation=8;g_powerLifecycle=PowerLifecycle::Resuming;
  manualLease=!unknown;manualMarkerIoFailed=unknown;runProductionHibernateWake(8);
  assert(resumeCalls==0&&freezeDepth==1&&markerExists&&g_sgSleepTarget.markerOwned&&(manualLease||manualMarkerIoFailed));
 }
 pass("S4 respects new manual pause and unknown manual marker state");
 for(unsigned boundary=0;boundary<5;++boundary){
  reset();freeze();g_powerGeneration=8;g_sgTask.generation=8;g_powerLifecycle=PowerLifecycle::Resuming;resumeFails=true;
  resumeDelayHook=[boundary]{
   resumeFails=false;
   if(boundary==0){g_powerGeneration=9;g_sgTask.generation=9;g_powerLifecycle=PowerLifecycle::Suspending;}
   if(boundary==1)g_exitRequested=true;
   if(boundary==2)manualLease=true;
   if(boundary==3){g_sgRetryInProgress=true;g_sgTask.retryKind=SgRetryKind::EntryFailure;}
   if(boundary==4)g_powerLifecycle=PowerLifecycle::Suspended;
  };
  runProductionHibernateWake(8);
  assert(resumeCalls==0&&freezeDepth==1&&markerExists&&g_sgSleepTarget.markerOwned&&focusCalls==0);
  if(boundary==0)assert(g_sgTask.generation==9&&g_sgInSuspend);
 }
 pass("S4 retry delay rechecks new sleep, exit, manual pause, retry and suspended phase before resume");
 for(bool replaceOwner:{false,true}){
  reset();freeze();g_powerGeneration=8;g_sgTask.generation=8;g_powerLifecycle=PowerLifecycle::Resuming;resumeFails=true;
  resumeDelayHook=[replaceOwner]{resumeFails=false;markerGeneration=9;if(replaceOwner)g_sgSleepTarget.powerGeneration=9;};
  runProductionHibernateWake(8);
  assert(resumeCalls==0&&freezeDepth==1&&markerExists&&markerGeneration==9&&g_sgSleepTarget.markerOwned);
 }
 pass("S4 retry preserves replacement marker and owner instead of treating them as stale cleanup");
 reset();freeze();g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;swapOnOpen=true;
 runProductionHibernateWake(8);assert(resumeCalls==0&&markerExists&&g_sgSleepTarget.markerOwned);
 pass("S4 refuses replaced process identity on opened handle and retains responsibility");
 reset();freeze();g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;
 resumeOpenHook=[]{g_powerGeneration=9;g_sgTask.generation=9;g_powerLifecycle=PowerLifecycle::Suspending;};
 runProductionHibernateWake(8);assert(resumeCalls==0&&freezeDepth==1&&markerExists&&g_sgTask.generation==9);
 pass("S4 checks boundary immediately before the actual resume call");
 reset();freeze();g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;
 resumeDrainHook=[]{g_powerGeneration=9;g_sgTask.generation=9;g_powerLifecycle=PowerLifecycle::Suspending;};
 runProductionHibernateWake(8);assert(resumeCalls==1&&freezeDepth==0&&markerExists&&g_sgSleepTarget.markerOwned&&g_sgTask.generation==9&&g_sgInSuspend);
 pass("new sleep after accepted API keeps marker and new task without stale S4 cleanup");
 reset();freeze();g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;resumeFails=true;
 runProductionHibernateWake(8);assert(resumeCalls==0&&freezeDepth==1&&markerExists&&g_sgSleepTarget.markerOwned);
 resumeFails=false;g_exitRequested=true;g_powerLifecycle=PowerLifecycle::Suspending;
 sgResumeSleepTarget(7,false);assert(resumeCalls==1&&freezeDepth==0&&!markerExists&&!g_sgSleepTarget.markerOwned);
 pass("failed S4 keeps exact responsibility and original exit cleanup can still return it");
 reset();freeze();g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;markerIoFailed=true;
 runProductionHibernateWake(8);assert(resumeCalls==0&&markerExists&&g_sgSleepTarget.markerOwned);
 markerIoFailed=false;runProductionHibernateWake(8);assert(resumeCalls==1&&!markerExists&&!g_sgSleepTarget.markerOwned);
 pass("S4 marker read failure retains lease for a later valid S4 retry");
 reset();freeze();g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;g_sgWakeRecoveryPending=true;g_sgWakeRecovery.generation=8;
 g_sgWakeRecovery.entries.push_back({SG_SLEEP_LEASE_DIR,100,markerCreated,markerGeneration,"pid=100|created="+std::to_string(markerCreated)+"|generation="+std::to_string(markerGeneration)+"|state=suspended",false});
 runProductionHibernateWake(8);assert(resumeCalls==0&&g_sgWakeRecoveryPending&&recoveryCancelCalls==0&&markerExists);
 g_sgWakeRecovery.generation=7;runProductionHibernateWake(8);assert(resumeCalls==1&&!g_sgWakeRecoveryPending&&recoveryCancelCalls==1&&!markerExists&&focusCalls==0);
 pass("same-generation recovery retains ownership while old recovery retires before exact S4 return");
 reset();freeze();g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;g_sgWakeRecoveryPending=true;g_sgWakeRecovery.generation=8;
 g_sgWakeRecovery.entries.push_back({SG_SLEEP_LEASE_DIR,100,markerCreated,markerGeneration,"pid=100|created="+std::to_string(markerCreated)+"|generation="+std::to_string(markerGeneration)+"|state=suspended",true});
 runProductionHibernateWake(8);assert(resumeCalls==1&&!markerExists&&!g_sgSleepTarget.markerOwned&&g_sgWakeRecoveryPending&&recoveryCancelCalls==0&&g_sgWakeRecovery.entries.front().done);
 pass("same-generation pending batch with a done entry cannot strand its retained sleep lease");
 for(bool introduceManual:{false,true}){
  reset();freeze();g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;
  resumeOpenHook=[introduceManual]{resumeMarkerReadHook=[introduceManual]{if(introduceManual)manualLease=true;else{g_powerGeneration=9;g_sgTask.generation=9;g_powerLifecycle=PowerLifecycle::Suspending;}};};
  runProductionHibernateWake(8);assert(resumeCalls==0&&ntResumeAttempts==0&&freezeDepth==1&&markerExists&&g_sgSleepTarget.markerOwned);
 }
 pass("final marker read cannot hide a new power boundary or manual pause before NtResume");
 reset();freeze();freezeDepth=3;g_powerGeneration=8;g_powerLifecycle=PowerLifecycle::Resuming;
 resumeDrainHook=[]{g_powerGeneration=9;g_sgTask.generation=9;g_powerLifecycle=PowerLifecycle::Suspending;};
 runProductionHibernateWake(8);assert(resumeCalls==1&&ntResumeAttempts==1&&freezeDepth==2&&markerExists&&g_sgSleepTarget.markerOwned&&g_sgTask.generation==9);
 pass("actual NtResume drain stops before another call after a new suspend boundary");
 std::cout<<"native sleep-game pause selftest:''')
checks=checks.replace(' std::cout<<"native sleep-game pause selftest:',r"""
 reset();runProductionSuspendPause(7);assert(suspendCalls==1&&freezeDepth==1&&markerExists&&g_sgInSuspend&&g_sgGameActuallySuspended);
 pass("fresh production suspend dispatch publishes both flags after exact freeze");
 reset();g_sgInSuspend=true;g_sgGameActuallySuspended=true;runProductionSuspendPause(7);assert(suspendCalls==1&&freezeDepth==1&&markerExists&&g_sgInSuspend&&g_sgGameActuallySuspended);
 pass("stale flags without owner cannot bypass new production suspend dispatch");
 reset();runProductionSuspendPause(7);runProductionSuspendPause(7);assert(suspendCalls==1&&freezeDepth==1&&g_sgInSuspend&&g_sgGameActuallySuspended);
 pass("duplicate production suspend dispatch does not stack suspension");
 reset();freeze();g_powerGeneration=8;runProductionSuspendPause(8);assert(suspendCalls==1&&freezeDepth==1&&g_sgSleepTarget.powerGeneration==7&&g_sgSleepTarget.markerOwned&&g_sgInSuspend&&g_sgGameActuallySuspended);
 pass("re-sleep keeps still-owned old generation without another suspend count");
 reset();g_sgPauseResume=false;runProductionSuspendPause(7);assert(suspendCalls==0&&!g_sgInSuspend&&!g_sgGameActuallySuspended);
 pass("disabled pause option is not mistaken for a successful freeze");
 std::cout<<"native sleep-game pause selftest:""")
checks=checks.replace(' std::cout<<"native sleep-game pause selftest:',r'''
 for(bool caps:{false,true})for(bool enabled:{false,true}){
  reset();g_machineIdentity.family=YmccFamily::ValveSteamMachine;aoac=caps;g_sgPauseResume=enabled;
  assert(sgShouldKeepInputDuringStandby()==caps);
  runProductionSuspendPause(7);
  assert(suspendCalls==int(enabled)&&freezeDepth==int(enabled));
  assert(g_sgInSuspend==enabled&&g_sgGameActuallySuspended==enabled);
  assert(powerReleaseCalls==0&&resetCalls==0);
 }
 pass("new ValveSteamMachine desktop retains caps-dependent input and shared PID pause");
 reset();SleepSteamRecovery::repeatedSuspendGeneration=7;SleepSteamRecovery::repeatedSuspendEpoch=11;
 sgAbortSleepIntent("episode-context", 0);
 assert(SleepSteamRecovery::repeatedSuspendGeneration==0&&SleepSteamRecovery::repeatedSuspendEpoch==0&&g_powerLifecycle==PowerLifecycle::Ready);
 pass("cancel closes the exact extracted pure suspend episode");
 reset();g_fanPhysicalWake.recordSleep(7,100);g_fanPhysicalWake.recordSleep(7,200);
 const auto fanGeneration=currentFanPowerGeneration();assert(fanGeneration!=currentPowerGeneration());
 sgAbortSleepIntent("fan-generation-context", 0);
 // Both actual Ready notifications are required exactly once, with separate
 // Fan and shared-power generations; an unrelated event cannot hide a duplicate.
 assert(currentPowerGeneration()==7&&emittedEvents.size()==2);
 assert(emittedEvents[0].first=="fan.resume-ready"&&emittedEvents[0].second.at("generation").get<unsigned long long>()==fanGeneration&&emittedEvents[0].second.at("source")=="sleep-intent-canceled");
 assert(emittedEvents[1].first=="gameMedia.resume-ready"&&emittedEvents[1].second.at("generation").get<unsigned long long>()==7&&emittedEvents[1].second.at("source")=="sleep-intent-canceled");
 pass("cancel emits the actual pure fan generation without a second shared transaction");
 reset();sgHandheldResumeInputOnWake("oem-memory-port");
 assert(oemWakePortCalls.size()==1&&oemWakePortCalls[0].generation==7&&oemWakePortCalls[0].reason=="oem-memory-port"&&oemWakePortCalls[0].sensorsBefore==0&&sensorCalls==1&&resumeCalls==0);
 pass("input wake supplies generation and reason to counted OEM boundary before sensors");
 std::cout<<"native sleep-game pause selftest:''')
receipt_checks = r'''
unsigned pauseReceiptCount(){
 unsigned count=0;for(const auto& fact:facts)if(fact.first=="sleep-game-pause-receipt")++count;return count;
}
const json& pauseReceipt(){
 assert(pauseReceiptCount()==1);
 for(const auto& fact:facts)if(fact.first=="sleep-game-pause-receipt")return fact.second;
 std::abort();
}
const json& ordinaryPause(){
 unsigned count=0;const json* result=nullptr;
 for(const auto& fact:facts)if(fact.first=="sleep-game-pause"){++count;result=&fact.second;}
 assert(count==1&&result);return *result;
}
void runPauseReceiptChecks(){
 unsigned checked=0;
 // OFF retains the original ordinary result and performs no new probe/event.
 reset();runProductionSuspendPause(7);
 assert(suspendCalls==1&&freezeDepth==1&&ordinaryPause().at("paused")==true);
 assert(pauseThreadQueries==0&&pauseReceiptCount()==0);
 sgRecordSleepPauseReceipt(7,{100,0,true},detected,11,"direct-gate-check",json::object());
 assert(pauseThreadQueries==0&&pauseReceiptCount()==0);++checked;
 // Actual receipt + wrapper, with an explicitly mocked NT invocation terminal.
 reset();pauseDetailLogging=true;runProductionSuspendPause(7);
 const auto success=pauseReceipt();const auto verified=success.at("threadVerification");
 assert(suspendCalls==1&&freezeDepth==1&&ordinaryPause().at("paused")==true&&pauseThreadQueries==1);
 assert(success.at("ntApiAttempted")==true&&success.at("ntStatusKnown")==true&&success.at("ntApiAccepted")==true);
 assert(success.at("existingLeaseReused")==false&&success.at("reason")=="nt-suspend-accepted");
 assert(success.at("invocation").at("ntStatus")==0&&verified.at("attempted")==true&&verified.at("known")==true);
 assert(verified.at("threads")==3&&verified.at("suspendedThreads")==3&&verified.at("allSuspended")==true);
 assert(verified.at("snapshotKnown")==true&&verified.at("snapshotAllSuspended")==true);
 assert(verified.at("status")=="all-threads-suspended");++checked;
 for(bool known:{false,true}){
  reset();pauseDetailLogging=true;WakeGameResume::pauseThreadState={known,147,known?146u:0u};
  runProductionSuspendPause(7);const auto& v=pauseReceipt().at("threadVerification");
  assert(suspendCalls==1&&freezeDepth==1&&pauseThreadQueries==1&&v.at("attempted")==true);
  assert(v.at("threads")==147&&v.at("known")==known);
  if(known){assert(v.at("allSuspended")==false&&v.at("status")=="not-all-threads-suspended");
    assert(v.at("snapshotKnown")==true&&v.at("snapshotAllSuspended")==false);}
  else{assert(v.at("allSuspended").is_null()&&v.at("status")=="thread-state-unknown");
    assert(v.at("snapshotKnown")==false&&v.at("snapshotAllSuspended").is_null());}
  ++checked;
 }
 reset();freeze();pauseDetailLogging=true;const auto duplicate=sgPauseSleepTarget(7);
 assert(duplicate.frozen&&suspendCalls==1&&freezeDepth==1&&pauseThreadQueries==1);
 assert(pauseReceipt().at("reason")=="existing-sleep-lease"&&pauseReceipt().at("existingLeaseReused")==true);
 assert(pauseReceipt().at("ntApiAttempted")==false&&pauseReceipt().at("ntStatusKnown")==false&&pauseReceipt().at("ntApiAccepted").is_null());++checked;
 reset();pauseDetailLogging=true;targetAvailable=false;runProductionSuspendPause(7);
 assert(suspendCalls==0&&freezeDepth==0&&ordinaryPause().at("pid")==0&&pauseThreadQueries==0);
 assert(pauseReceipt().at("reason")=="no-current-valve-target"&&pauseReceipt().at("paused")==false);
 assert(pauseReceipt().at("threadVerification").at("allSuspended").is_null());++checked;
 // Revoke each factual guard during the one read; no stale sample is confirmed.
 for(unsigned change=0;change<3;++change){
  reset();pauseDetailLogging=true;
  pauseThreadProbeHook=[change]{if(change==0)++actualCreated;else if(change==1)++g_powerGeneration;else g_powerLifecycle=PowerLifecycle::Resuming;};
  const auto result=sgPauseSleepTarget(7);const auto& v=pauseReceipt().at("threadVerification");
  assert(result.frozen&&suspendCalls==1&&freezeDepth==1&&pauseThreadQueries==1);
  assert(v.at("attempted")==true&&v.at("known")==false&&v.at("allSuspended").is_null());
  assert(v.at("snapshotKnown")==true&&v.at("snapshotAllSuspended")==true);
  assert(v.at("status")=="context-or-identity-changed");++checked;
 }
 reset();pauseDetailLogging=true;
 pauseInvocationOverride={{"paused",false},{"okCount",0},{"failCount",1},{"ntStatus",-1073741790LL},{"pauseReason","nt-suspend-rejected"}};
 runProductionSuspendPause(7);const auto failure=pauseReceipt();
 assert(suspendCalls==1&&freezeDepth==0&&!markerExists&&pauseThreadQueries==0&&ordinaryPause().at("paused")==false);
 assert(failure.at("ntApiAttempted")==true&&failure.at("ntStatusKnown")==true&&failure.at("ntApiAccepted")==false);
 assert(failure.at("invocation").at("ntStatus")==-1073741790LL&&failure.at("reason")=="nt-suspend-rejected");
 assert(failure.at("threadVerification").at("allSuspended").is_null());++checked;
 assert(checked==10);
 std::cout<<"pause receipt detail checks: "<<checked<<" PASS (actual helper/wrapper; NT invocation and thread OS ports mocked)\n";
}
'''
pause_exit_admission_checks = r"""
void runPauseExitAdmissionChecks(){
 unsigned checked=0;
 // These are synchronous wrapper admission boundaries. Cleanup, real worker
 // interleavings and real NT suspension remain outside this memory fixture.
 reset();g_exitRequested.store(true,std::memory_order_release);
 const auto early=sgPauseSleepTarget(7);
 assert(!early.frozen&&early.pid==0&&!g_sgSleepTarget.captured&&!g_sgSleepTarget.markerOwned);
 assert(pauseMetadataCalls==0&&!markerExists&&suspendCalls==0&&freezeDepth==0);++checked;
 reset();pauseMetadataHook=[]{g_exitRequested.store(true,std::memory_order_release);};
 const auto afterEntry=sgPauseSleepTarget(7);
 assert(!afterEntry.frozen&&afterEntry.pid==0&&pauseMetadataCalls==1&&g_exitRequested.load(std::memory_order_acquire));
 // The existing capture before the op lock may remain; it grants no lease.
 assert(!g_sgSleepTarget.markerOwned&&!markerExists&&suspendCalls==0&&freezeDepth==0);++checked;
 assert(checked==2);
 std::cout<<"pause exit admission checks: "<<checked<<" PASS (synchronous metadata port; no cleanup or thread interleaving)\n";
}
"""
assert checks.count('int main(){') == 1
checks = checks.replace('int main(){', receipt_checks + pause_exit_admission_checks + '\nint main(){', 1)
assert checks.count(' std::cout<<"native sleep-game pause selftest:') == 1
checks = checks.replace(' std::cout<<"native sleep-game pause selftest:', ' assert(!pauseUnselectedPolicyReached);\n runPauseReceiptChecks();\n runPauseExitAdmissionChecks();\n std::cout<<"native sleep-game pause selftest:', 1)
(out/'source-bindings.json').write_text(report_json.dumps({
 'sourceSha256':hashlib.sha256(args.source.read_bytes()).hexdigest(),
 'scriptSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
 'extractedDefinitions':extracted,
 'actualMainConsumerFunctions':names,
 'additionalPureMainFunction':'currentFanPowerGeneration',
 'additionalPureHeaderFunctions':['SleepSteamRecovery::closeSuspendEpisode','sleepRecoveryAutoCloseSuspendEpisode','sgReconcileSleepGameFlagsAfterRecoveryCancel'],
 'resumeDrainExecution':{'actualFunctions':['sgDrainProcessSuspendCountIfCurrent','sgDrainProcessSuspendCount'],
  'ntResumePort':'counted memory-only function; no process resume API',
  'cancellationBoundary':'final marker read and each individual resume attempt'},
 'sourceOnlySuspendHelper':'sgSuspendGameByPidUnlocked',
 'pauseReceiptExecution':{'helper':'sgRecordSleepPauseReceipt','wrapper':'sgPauseSleepTarget',
  'supplementaryChecks':10,'detailOff':'no new event or thread query',
  'threadQuery':'counted memory port; no Windows thread enumeration',
  'ntInvocation':'injected memory receipt; actual bottom helper not executed'},
 'originalNativeAssertionStatements':48,'originalPassGroups':24,
 'pauseExitAdmissionExecution':{'wrapper':'sgPauseSleepTarget','supplementaryChecks':2,
  'metadataPort':'counted synchronous target.txt memory hook',
  'boundaries':['exit already requested before call','exit raised by metadata port before op lock'],
  'scope':'wrapper admission only; no cleanup or real thread interleaving; bottom NT helper remains source-only'},
 'newFamilySeparated':True,'previousFamilyNames':previous_family_names,'newDesktopFamily':'ValveSteamMachine',
 'ownCancellationThread':{'waitMs':25,'joined':True},
 'oemPort':'counted generation/reason/order only; no OEM production scheduler',
 'realProcessOrSleepOperations':False
},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
if args.returned_pause_bottom_only:
    # Replace only the old counted suspend reimplementation. All complete
    # production layers now execute; the NT/marker/session leaves stay inert.
    opening=preamble.index('json sgSuspendGameByPidUnlocked(')
    body=preamble.index('{',opening);end=body+1;depth=1
    while depth:depth+=(preamble[end]=='{')-(preamble[end]=='}');end+=1
    original=function('sgSuspendGameByPidUnlocked')
    declaration=original[:original.index('{')].strip()
    declaration=declaration.replace('const std::function<bool()>& requestCurrent,','const std::function<bool()>& requestCurrent = std::function<bool()>(),').replace('const std::function<LONG(HANDLE, bool&, ULONGLONG&)>& suspendCurrent)','const std::function<LONG(HANDLE, bool&, ULONGLONG&)>& suspendCurrent = std::function<LONG(HANDLE, bool&, ULONGLONG&)>())')
    preamble=preamble[:opening]+declaration+';\n'+preamble[end:]
    preamble=preamble.replace('using HANDLE = void*; using WPARAM = unsigned long long;','using HANDLE = void*; using WPARAM = unsigned long long; using LONG=long;')
    preamble=preamble.replace('ULONGLONG nativeProcessCreatedFromHandle(HANDLE){return actualCreated;}','ULONGLONG nativeProcessCreatedFromHandle(HANDLE){if(pauseFinalHook){auto hook=std::exchange(pauseFinalHook,{});hook();}return actualCreated;}')
    # The original final NT call is counted here; no real process API exists.
    extra=r"""
std::atomic<unsigned long long> g_sgManualPauseSerial{0};
bool nativeValveValidateCurrentTarget(DWORD pid,ULONGLONG created,NativeDetectedGame* out){if(pid!=detected.pid||created!=actualCreated)return false;*out=detected;return true;}
// These cases own one in-memory marker and never run startup or peer cleanup.
void sgCollectStartupResumeScopeUnlocked(){}
bool sgPrepareWakeRecoveryForPauseUnlocked(DWORD,const std::function<bool()>& current){return !current||current();}
bool sgReleaseConflictingPauseLeaseUnlocked(DWORD,const std::function<bool()>& current){return !current||current();}
template<class Current>
static ymcc::sleepLegacyResume::ResumeLease sgAcquirePauseFiles(
 const std::wstring& dir,DWORD pid,const Current& current) {
 assert(dir==SG_SLEEP_LEASE_DIR&&pid==100);
 return {current()?ymcc::sleepLegacyResume::Result::None:ymcc::sleepLegacyResume::Result::Pending,0};
}
ULONGLONG sgMarkerCreated(const std::wstring&,DWORD){return markerCreated;}
bool sgMarkerIsSuspended(const std::wstring&,DWORD){return markerExists;}
bool sgWriteProcessMarker(const std::wstring&,DWORD,const char*,ULONGLONG generation){markerExists=true;markerCreated=actualCreated;markerGeneration=generation;return true;}
namespace fspath {bool exists(const std::wstring&){return markerExists;}void remove(const std::wstring&){markerExists=false;}}
long pauseActualNtPort(HANDLE h){assert(h==reinterpret_cast<HANDLE>(100));++suspendCalls;++freezeDepth;return 0;}
auto fnNtSuspend=&pauseActualNtPort;
"""
    # pauseFinalHook must exist before the earlier opened-handle reader.
    preamble=preamble.replace('std::function<void()> pauseFinalHook;','')
    preamble=preamble.replace('std::function<void()> pauseHook;','std::function<void()> pauseHook,pauseFinalHook;')
    preamble+=extra
    functions=original+'\n'+functions

if args.ordinary_pause_only:
    # Execute the actual queue and Steam fan-out; replace only its three sinks.
    fake='void sgQueueWork(SgWork work,ULONGLONG gen,ULONGLONG=0){queue.emplace_back(work,gen);}'
    assert preamble.count(fake)==1
    ports=r"""
static SgWorkItem g_sgLastQueuedGameWake{};
static unsigned ordinarySteamCaptures=0,ordinarySteamReadyCalls=0,ordinarySteamOriginalCalls=0,ordinarySteamFault=0;
static ULONGLONG ordinarySteamBoundary=0;
static std::function<void()> ordinaryCaptureHook;
namespace SleepSteamRecovery {
static void captureSuspend(ULONGLONG,const std::shared_ptr<void>&,ULONGLONG boundary){ordinarySteamBoundary=boundary;++ordinarySteamCaptures;if(ordinaryCaptureHook){auto hook=std::exchange(ordinaryCaptureHook,{});hook();}if(ordinarySteamFault==1)throw std::bad_alloc();}
}
namespace SleepRecoveryAuto {
static void captureReady(ULONGLONG){++ordinarySteamReadyCalls;if(ordinarySteamFault==2)throw std::bad_alloc();}
}
static void steamOriginalRecoverySuspend(ULONGLONG){++ordinarySteamOriginalCalls;if(ordinarySteamFault==3)throw std::bad_alloc();}
static void sgQueueWork(SgWork,unsigned long long,ULONGLONG=0);
"""
    preamble=preamble.replace(fake,ports)
    steam_auto=args.source.parent/'sleep_recovery_auto.h'
    functions+='\n'+function('sleepRecoveryAutoCaptureSuspend',steam_auto.read_text(encoding='utf-8-sig'),'native/sleep_recovery_auto.h')+'\n'+function('sgQueueWork')+'\n'

if args.returned_pause_only:
    choices=['normal','newer-entry','duplicate-queue','ordinary-already-queued','manual-before-queue','manual-after-queue','manual-and-new-entry','manual-at-metadata','manual-at-final-nt','changed-generation','phase-ready','exit-after-queue','pause-disabled','target-changed','partial-return','unknown-return','queue-stopped','option-at-metadata','guard-at-final-nt','repair-at-final-nt', 'public-auto-after-queue', 'public-user-after-queue', 'public-critical-after-queue', 'public-auto-at-metadata', 'public-auto-final-nt', 'public-auto-post-failed', 'public-auto-during-return', 'public-old-before-entry', 'public-unknown-before-queue', 'public-clock-back-after-queue', 'public-suspend-only', 'public-successor-after-stale-returned', 'public-successor-after-stale-ordinary']
    if args.returned_pause_case is not None and args.returned_pause_case not in choices:parser.error('unknown returned-pause case')
    selected=[args.returned_pause_case] if args.returned_pause_case is not None else choices
    reset_end=checks.index('int main(){')
    checks=checks[:reset_end]+r"""
static void returnedReset(){
 reset();g_sgWorkQ.clear();g_sgWorkStop=false;g_sgWorkWakeRevision=0;pauseFinalHook={};returnedPublicReset();
 g_sgTask.generation=7;g_sgTask.retryEntryFileTime=100;
 g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgRetryInProgress=true;
 g_sgLastPowerButtonSleepIntentFileTime=100;g_sgLatestSleepEntryFileTime=100;
 g_sgLatestUserSleepEntryFileTime=100;g_sgOriginalPowerButtonWakeFileTime=200;
 g_sgLastKernel506EventFileTime=100;g_sgLastKernel507EventFileTime=200;
 g_hardwareWriteGate=false;g_sgSleepTriggerTick=1;
}
int main(){
 const std::vector<std::string> selected=SELECTED_VALUES;
 json observations=json::array();
 for(const auto& scenario:selected){
  std::cerr<<"RETURNED_PAUSE_BEGIN "<<scenario<<std::endl;
  returnedReset();freeze();
  if(scenario=="public-old-before-entry")returnedPublicPublish(18,50,true);
  if(scenario=="public-unknown-before-queue")returnedPublicPublish(18,0,true);
  if(scenario=="partial-return")freezeDepth=3;
  if(scenario=="ordinary-already-queued")g_sgWorkQ.push_back({SgWork::Suspend,7});
  if(scenario=="queue-stopped")g_sgWorkStop=true;
  resumeDrainHook=[scenario]{
   g_sgLatestSleepEntryFileTime=300;g_sgLatestUserSleepEntryFileTime=300;
   g_sgLastKernel506EventFileTime=300;
   WakeGameResume::pauseThreadState={scenario!="unknown-return",3,scenario=="partial-return"?3U:0U};
   if(scenario=="manual-before-queue")g_sgOriginalPowerButtonWakeFileTime=350;
   if(scenario=="public-auto-during-return")returnedPublicPublish(18,350,true);
  };
  sgAbortSleepIntent("s0-user-power-button-wake",200);
  const bool debt= scenario=="partial-return"||scenario=="unknown-return";
  const bool noQueue=debt||scenario=="manual-before-queue"||scenario=="queue-stopped"||scenario=="public-auto-during-return"||scenario=="public-unknown-before-queue";
  assert(g_sgWorkQ.size()==(noQueue?0U:1U));
  assert(markerExists==debt&&g_sgSleepTarget.markerOwned==debt);
  if(!noQueue){
   SgWorkItem scope=g_sgWorkQ.front();
   assert(scope.generation==7);
   if(scenario!="ordinary-already-queued")assert(scope.pauseEntryFileTime==300&&scope.pauseWakeFileTime==200&&scope.pausePid==100&&scope.pauseProcessCreated==1234);
   if(scenario=="public-successor-after-stale-returned"||scenario=="public-successor-after-stale-ordinary"){
    // Seed only a previously accepted queue boundary; original queue admission,
    // duplicate policy and worker/pause functions below still execute verbatim.
    if(scenario=="public-successor-after-stale-ordinary"){
     auto& pending=g_sgWorkQ.front();pending.pauseEntryFileTime=0;
     pending.pauseOrdinaryWitness=true;pending.pauseTriggerFileTime=300;
    }
    returnedPublicPublish(18,350,true);g_sgLatestSleepEntryFileTime=400;
    auto successor=scope;successor.pauseEntryFileTime=400;
    successor.pauseResumeSequence=g_sleepPowerDispatch.latestResumeBoundary().sequence;
    sgQueueReturnedPause(successor);assert(g_sgWorkQ.size()==2);
    const auto stale=g_sgWorkQ.front();g_sgWorkQ.pop_front();
    runProductionSuspendPause(stale.generation,&stale);assert(suspendCalls==1&&freezeDepth==0);
    scope=g_sgWorkQ.front();
   }
   if(scenario=="duplicate-queue"){sgQueueReturnedPause(scope);sgQueueReturnedPause(scope);assert(g_sgWorkQ.size()==1);}
   if(scenario=="newer-entry")g_sgLatestSleepEntryFileTime=400;
   if(scenario=="manual-after-queue"||scenario=="manual-and-new-entry")g_sgOriginalPowerButtonWakeFileTime=350;
   if(scenario=="manual-and-new-entry")g_sgLatestSleepEntryFileTime=400;
   if(scenario=="changed-generation")g_powerGeneration=8;
   if(scenario=="phase-ready")g_powerLifecycle=PowerLifecycle::Ready;
   if(scenario=="exit-after-queue")g_exitRequested=true;
   if(scenario=="pause-disabled")g_sgPauseResume=false;
   if(scenario=="target-changed")detected.pid=101;
   if(scenario=="public-auto-after-queue")returnedPublicPublish(18,350,true);
   if(scenario=="public-user-after-queue")returnedPublicPublish(7,350,true);
   if(scenario=="public-critical-after-queue")returnedPublicPublish(6,350,true);
   if(scenario=="public-auto-post-failed")returnedPublicPublish(18,350,false);
   if(scenario=="public-clock-back-after-queue")returnedPublicPublish(18,50,true);
   if(scenario=="public-suspend-only")returnedPublicPublish(4,350,true);
   if(scenario=="public-auto-at-metadata")pauseMetadataHook=[]{returnedPublicPublish(18,350,true);};
   if(scenario=="public-auto-final-nt")pauseFinalHook=[]{returnedPublicPublish(18,350,true);};
   if(scenario=="manual-at-metadata")pauseMetadataHook=[]{g_sgOriginalPowerButtonWakeFileTime=350;};
   if(scenario=="manual-at-final-nt")pauseFinalHook=[]{g_sgOriginalPowerButtonWakeFileTime=350;};
   if(scenario=="option-at-metadata")pauseMetadataHook=[]{g_sgPauseResume=false;};
   if(scenario=="guard-at-final-nt")pauseFinalHook=[]{g_guardEnabled=false;};
   if(scenario=="repair-at-final-nt")pauseFinalHook=[]{g_sgRepairEligible=false;};
   g_sgWorkQ.pop_front();
   runProductionSuspendPause(scope.generation,&scope);
  }
  const bool expectedPause=scenario=="normal"||scenario=="newer-entry"||scenario=="duplicate-queue"||scenario=="ordinary-already-queued"||scenario=="public-old-before-entry"||scenario=="public-suspend-only"||scenario=="public-successor-after-stale-returned"||scenario=="public-successor-after-stale-ordinary";
  assert(suspendCalls==(expectedPause?2:1));
  assert(freezeDepth==(debt?(scenario=="partial-return"?2:0):(expectedPause?1:0)));
  assert(markerExists==(debt||expectedPause));
  if(expectedPause)assert(g_sgInSuspend&&g_sgGameActuallySuspended&&g_sgSleepTarget.markerOwned);
  // A resource continuation never calls the ordinary queue/capture port.
  assert(queue.empty()&&powerReleaseCalls==0&&resetCalls==0);
  const auto observedResume=g_sleepPowerDispatch.latestResumeBoundary();
  observations.push_back({{"scenario",scenario},{"publicResumeSequence",observedResume.sequence},{"publicResumeTime",observedResume.fileTime100ns},{"expectedPause",expectedPause},{"suspendCalls",suspendCalls},{"resumeCalls",resumeCalls},{"freezeDepth",freezeDepth},{"markerOwned",g_sgSleepTarget.markerOwned},{"ordinaryQueueCalls",queue.size()},{"workerQueueRemaining",g_sgWorkQ.size()},{"generation",currentPowerGeneration()}});
 }
 std::cout<<"RETURNED_PAUSE_MATRIX "<<json({{"status","PASS"},{"cases",observations},{"count",observations.size()},{"physicalCalls",0},{"default43Executed",false}}).dump()<<'\n';return 0;
}
""".replace('SELECTED_VALUES','{'+','.join(report_json.dumps(x) for x in selected)+'}')

if args.ordinary_pause_only:
    cases=['normal', 'manual-after-queue', 'manual-during-capture', 'manual-at-metadata', 'manual-before-final-nt', 'manual-before-queue', 'manual-and-new-entry', 'old-manual-before-new-suspend', 'no-original-receipt-no-manual', 'no-original-receipt-after-manual', 'future-suspend-receipt', 'resume-before-queue', 'resume-after-queue', 'resume-at-final-nt', 'old-resume-before-new-suspend', 'duplicate-suspend-boundary', 'guard-off-still-capture', 'pause-off-still-capture', 'generation-after-queue', 'exit-after-queue', 'option-at-metadata', 'resume-sequence-with-older-time', 'successor-after-stale-ordinary-resume', 'successor-after-stale-ordinary-manual', 'successor-after-stale-ordinary-both', 'successor-after-stale-returned-resume', 'successor-after-stale-returned-manual', 'successor-after-stale-returned-both', 'current-returned-already-queued', 'legacy-pause-already-queued']
    if args.ordinary_pause_case=='recovery-queue-ownership':cases=['recovery-queue-ownership']
    if args.ordinary_pause_case=='pause-completion-ownership':cases=['pause-completion-ownership']
    cases+=['steam-capture-failure','steam-ready-failure','steam-original-failure','steam-capture-failure-with-wake']
    if args.ordinary_pause_case is not None:
        if args.ordinary_pause_case not in cases:parser.error('unknown ordinary pause case')
        cases=[args.ordinary_pause_case]
    reset_end=checks.index('int main(){')
    checks=checks[:reset_end]+r"""
int main(){
 json observations=json::array();
 for(const auto& scenario:std::vector<std::string>CASE_VALUES){
  reset();g_sgWorkQ.clear();g_sgWorkStop=false;ordinarySteamCaptures=0;ordinaryCaptureHook={};pauseFinalHook={};returnedPublicReset();
  ordinarySteamReadyCalls=ordinarySteamOriginalCalls=0;
  ordinarySteamBoundary=0;
  ordinarySteamFault=scenario.starts_with("steam-capture-failure")?1:scenario=="steam-ready-failure"?2:scenario=="steam-original-failure"?3:0;
  g_sgTask.generation=7;g_sgRepairEligible=true;g_guardEnabled=true;
  g_sgLastPowerButtonSleepIntentFileTime=200;g_sgLatestSleepEntryFileTime=200;
  g_sgLatestUserSleepEntryFileTime=200;g_sgOriginalPowerButtonWakeFileTime=100;
  if(scenario=="manual-during-capture"||scenario=="steam-capture-failure-with-wake")ordinaryCaptureHook=[]{g_sgOriginalPowerButtonWakeFileTime=300;};
  if(scenario=="manual-before-queue")g_sgOriginalPowerButtonWakeFileTime=300;
  if(scenario=="no-original-receipt-no-manual")g_sgOriginalPowerButtonWakeFileTime=0;
  if(scenario=="resume-before-queue")returnedPublicPublish(18,300,true);
  if(scenario=="old-resume-before-new-suspend")returnedPublicPublish(18,50,true);
  if(scenario=="guard-off-still-capture")g_guardEnabled=false;
  if(scenario=="pause-off-still-capture")g_sgPauseResume=false;
  const auto original=scenario=="future-suspend-receipt"?2000000000ULL:
      scenario.starts_with("no-original-receipt")?0ULL:200ULL;
  const bool successor=scenario.starts_with("successor-after-stale-");
  if(scenario.find("stale-returned")!=std::string::npos||scenario=="current-returned-already-queued")g_sgWorkQ.push_back({SgWork::Suspend,7,200,100,100,1234});
  if(scenario=="legacy-pause-already-queued")g_sgWorkQ.push_back({SgWork::Suspend,7});
  try{sgQueueWork(SgWork::Suspend,7,original);}
  catch(...){std::cerr<<"Steam capture must not strand the public Suspend queue: "<<scenario<<'\n';return 1;}
  assert(!ordinarySteamCaptures||ordinarySteamBoundary==original);
  if(successor){
   assert(g_sgWorkQ.size()==1);
   if(!scenario.ends_with("manual"))returnedPublicPublish(18,300,true);
   if(!scenario.ends_with("resume"))g_sgOriginalPowerButtonWakeFileTime=300;
   g_sgLatestSleepEntryFileTime=400;g_sgLatestUserSleepEntryFileTime=400;g_sgLastPowerButtonSleepIntentFileTime=400;
   sgQueueWork(SgWork::Suspend,7,400);
   assert(ordinarySteamBoundary==400);
   const auto resume=g_sleepPowerDispatch.latestResumeBoundary();
   std::cout<<"ORDINARY_QUEUE_SUCCESSOR "<<json({{"scenario",scenario},{"queued",g_sgWorkQ.size()},{"resumeSequence",resume.sequence},{"resumeTime",resume.fileTime100ns},{"captures",ordinarySteamCaptures}}).dump()<<std::endl;
   assert(g_sgWorkQ.size()==2&&"current ordinary sleep swallowed by stale queued ticket");
  }
  if(scenario=="manual-after-queue")g_sgOriginalPowerButtonWakeFileTime=300;
  if(scenario=="manual-at-metadata")pauseMetadataHook=[]{g_sgOriginalPowerButtonWakeFileTime=300;};
  if(scenario=="manual-before-final-nt")pauseFinalHook=[]{g_sgOriginalPowerButtonWakeFileTime=300;};
  if(scenario=="manual-and-new-entry"){g_sgOriginalPowerButtonWakeFileTime=300;g_sgLatestSleepEntryFileTime=400;}
  if(scenario=="resume-after-queue")returnedPublicPublish(18,300,true);
  if(scenario=="resume-sequence-with-older-time")returnedPublicPublish(18,50,true);
  if(scenario=="duplicate-suspend-boundary")sgQueueWork(SgWork::Suspend,7,original);
  if(scenario=="resume-at-final-nt")pauseFinalHook=[]{returnedPublicPublish(18,300,true);};
  if(scenario=="generation-after-queue")g_powerGeneration=8;
  if(scenario=="exit-after-queue")g_exitRequested=true;
  if(scenario=="option-at-metadata")pauseMetadataHook=[]{g_sgPauseResume=false;};
  const auto queued=g_sgWorkQ.size();
  while(!g_sgWorkQ.empty()){const auto item=g_sgWorkQ.front();g_sgWorkQ.pop_front();runProductionSuspendPause(item.generation,&item);}
  observations.push_back({{"scenario",scenario},{"queued",queued},{"steamCaptures",ordinarySteamCaptures},{"suspendCalls",suspendCalls},{"freezeDepth",freezeDepth},{"markerOwned",g_sgSleepTarget.markerOwned},{"lastOriginalWake",g_sgOriginalPowerButtonWakeFileTime.load()}});
  const bool expectedPause=scenario=="normal"||scenario=="old-manual-before-new-suspend"||scenario=="no-original-receipt-no-manual"||scenario=="old-resume-before-new-suspend"||scenario=="duplicate-suspend-boundary"||successor||scenario=="current-returned-already-queued"||scenario=="legacy-pause-already-queued";
  const bool preRejected=scenario=="manual-before-queue"||scenario=="resume-before-queue"||scenario=="no-original-receipt-after-manual"||scenario=="future-suspend-receipt";
  assert(ordinarySteamCaptures==(preRejected?0U:(scenario=="duplicate-suspend-boundary"||successor)?2U:1U));
  assert(ordinarySteamReadyCalls==ordinarySteamCaptures&&ordinarySteamOriginalCalls==ordinarySteamCaptures);
  if(ordinarySteamFault){
   const std::string stage=ordinarySteamFault==1?"steam-original-capture":ordinarySteamFault==2?"steam-wake-fact-join":"steam-original-recovery";
   const auto found=std::count_if(facts.begin(),facts.end(),[&](const auto& fact){return fact.first=="sleep-suspend-consumer-failed"&&fact.second.at("consumer")==stage&&fact.second.at("generation")==7;});
   assert(found==1);
   assert(queued==(scenario=="steam-capture-failure-with-wake"?0U:1U));
  }
  const bool effectivePause=expectedPause||(ordinarySteamFault&&scenario!="steam-capture-failure-with-wake");
  assert(suspendCalls==(effectivePause?1:0)&&freezeDepth==(effectivePause?1:0)&&markerExists==effectivePause);
 }
 std::cout<<"ORDINARY_PAUSE_MATRIX "<<json({{"status","PASS_ORDINARY_MANUAL_WAKE_BOUNDARIES"},{"count",observations.size()},{"cases",observations},{"physicalCalls",0}}).dump()<<'\n';return 0;
}
""".replace('CASE_VALUES','{'+','.join(report_json.dumps(x) for x in cases)+'}')

if args.returned_pause_only:
    preamble='#define YMCC_RETURNED_PUBLIC_DISPATCH 1\n'+preamble
if args.ordinary_pause_only and args.ordinary_pause_case=='recovery-queue-ownership':
    import runpy
    preamble,functions,checks=runpy.run_path(str(Path(__file__).parent/'fixtures/sleep_non_user_wake/queue_recovery.py'))['pair'](globals())
if args.ordinary_pause_only and args.ordinary_pause_case=='pause-completion-ownership':
    import runpy
    preamble,functions,checks=runpy.run_path(str(Path(__file__).parent/'fixtures/sleep_non_user_wake/pause_completion.py'))['pair'](globals())
preamble,checks=runpy.run_path(str(Path(__file__).parent/'fixtures/sleep_non_user_wake/retained_delivery.py'))['adapt_checks'](globals())
if args.prepare_only:
    with (out/'sleep_game_pause_native_selftest.cpp').open('x',encoding='utf-8') as file:file.write(preamble+'\n'+functions+'\n'+checks)
    print(report_json.dumps({'status':'PREPARED_NOT_RUN','selected':args.ordinary_pause_case or args.returned_pause_case or 'original-default','defaultBusinessExecuted':False}));raise SystemExit(0)
cpp=out/'sleep_game_pause_native_selftest.cpp'
cpp.write_text(preamble+'\n'+functions+'\n'+checks,encoding='utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
assert vs.is_file(), 'MSVC build environment not found'
repo=Path.cwd()
cmd=out/'run-native-selftest.cmd'
exe=out/'sleep_game_pause_native_selftest.exe'
toolchain_js='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain_prefix=subprocess.check_output(['node','--input-type=module','-e',toolchain_js,str(vs)],text=True,encoding='utf-8')
public_compile=''
public_link=''
if args.returned_pause_only:
    bridge=repo/'tools/fixtures/sleep_non_user_wake/returned_public_dispatch.cpp'
    public_object=out/'returned_public_dispatch.obj'
    public_compile=f'cl /nologo /c /std:c++20 /utf-8 /EHsc /MT /W3 "{bridge}" /I"{repo / "deps/json"}" /I"{repo / "native"}" /Fo"{public_object}" /sourceDependencies "{out / "public-dispatch-dependencies.json"}"\r\nif errorlevel 1 exit /b 1\r\n'
    public_link=f' "{public_object}"'
cmd.write_text(toolchain_prefix+public_compile+f'if errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{cpp}" /I"{repo / "deps/json"}" /I"{repo / "native"}" /Fo"{out / "sleep_game_pause_native_selftest.obj"}" /Fe"{exe}" /sourceDependencies "{out / "dependencies.json"}"{public_link} /link /VERBOSE:LIB\r\nif errorlevel 1 exit /b 1\r\n"{exe}"\r\n',encoding='ascii')
result=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'native-selftest.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8',errors='replace'))
raise SystemExit(result.returncode)
