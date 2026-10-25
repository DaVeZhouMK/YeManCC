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
args = parser.parse_args()
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
    matches = list(re.finditer(r'\bstatic\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*\{', masked))
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
worker_pause = section('            if (item.kind == SgWork::Suspend)', '} else if (item.kind == SgWork::WakeAutomatic)')
worker_pause_gate = worker_pause[worker_pause.index('// S4/hibernate'):worker_pause.index('// The cycle remains active')]

assert 'sgShouldKeepInputDuringStandby' not in worker_pause
assert 'const SgSuspendResult result = sgPauseSleepTarget(item.generation);' in worker_pause
assert worker_pause.index('sgPrepareWakeRecoveryForSleep(item.generation);') < worker_pause.index('if (g_guardEnabled && g_sgRepairEligible')
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
assert abort.index('std::lock_guard<std::mutex> opLock(g_sgOpMtx)') < abort.index('sgReadSleepTarget')
assert 'sgResumeSleepTargetUnlocked(generation, true)' in abort
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
#include "sleep_request_fallback.h"
#include <utility>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
using DWORD = unsigned long; using ULONGLONG = unsigned long long;
using HANDLE = unsigned long;
constexpr bool FALSE = false;
constexpr int PROCESS_SUSPEND_RESUME = 1, PROCESS_QUERY_LIMITED_INFORMATION = 2;
constexpr DWORD GENERIC_READ=0x80000000ul, FILE_SHARE_READ=1, FILE_SHARE_WRITE=2, FILE_SHARE_DELETE=4;
constexpr DWORD OPEN_EXISTING=3, FILE_ATTRIBUTE_NORMAL=128, STILL_ACTIVE=259, ERROR_INVALID_PARAMETER=87;
constexpr HANDLE INVALID_HANDLE_VALUE=~HANDLE(0);
enum class PowerLifecycle { Ready, Suspending, Suspended, Resuming };
enum class YmccFamily { Unknown, Handheld };
enum class SgSleepMode { Unknown, S3, S4 };
enum class SgRetryKind { None, EntryFailure, UnexpectedWake };
enum class SgWork { Suspend, WakeAutomatic, WakeSuspend, WakeHibernate };
struct NativeDetectedGame { DWORD pid=100; ULONGLONG processCreated=1234; };
struct SgSleepTarget { DWORD pid=0; ULONGLONG processCreated=0, valveGeneration=0, powerGeneration=0; bool captured=false, markerOwned=false; };
struct SgSuspendResult { DWORD pid=0; uint64_t ws=0; bool frozen=false; };
struct SgResumeResult { int count=0; std::string names; std::vector<DWORD> pids; };
struct Task { ULONGLONG generation=7; SgSleepMode mode=SgSleepMode::S3; SgRetryKind retryKind=SgRetryKind::None; unsigned retryAttempt=0; bool unexpectedWakeConsumed=false; } g_sgTask;
struct { YmccFamily family=YmccFamily::Handheld; } g_machineIdentity;
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Suspending};
std::atomic<bool> g_sensorSuspendCallbackActive{false};
std::atomic<ULONGLONG> g_sgLastKernel506EventFileTime{0},g_sgLastKernel507EventFileTime{0};
std::atomic<ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget{ymcc::sleeprequest::Mode::None};
struct {SgRetryKind kind=SgRetryKind::None;ULONGLONG dispatchFileTime=0;} g_sgInternalSleepRequest;
std::atomic<ULONGLONG> g_powerGeneration{7}, g_resumeReadyGeneration{0}, g_sgPauseWorkCompletedGeneration{0};
std::atomic<bool> g_sgInSuspend{false}, g_sgGameActuallySuspended{false}, g_sgSleepCycleActive{true};
std::atomic<bool> g_inputReady{true}, g_inputHostPowerAdmissionRevoked{false}, g_wakeHoldPhysicalUnhide{false};
bool g_guardEnabled=true, g_sgPauseResume=true, g_sgRepairEligible=true, g_sgRetryInProgress=false;
bool g_sgModernStandbyActive=true, g_sgModernWakeClassified=false, g_sgSleepIntentArmed=false;
bool g_sgSleepQueryGateClosed=false, g_sgSleepQueryOwnsGeneration=false, g_sgPowerButtonSleepConfigured=true;
bool g_sgRetryEntryFailure=true;
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
ULONGLONG actualCreated=1234, markerCreated=1234, markerGeneration=7, nowTick=10000;
std::string markerReadBody;size_t markerReadOffset=0;
int freezeDepth=0, suspendCalls=0, resumeCalls=0, powerReleaseCalls=0, resetCalls=0, sensorCalls=0, rescanCalls=0, abortCalls=0;
std::vector<std::pair<SgWork,ULONGLONG>> queue;
std::function<void()> pauseHook;
std::atomic<bool> cancelEntered{false};
std::vector<std::pair<std::string,json>> facts;
ULONGLONG currentPowerGeneration(){return g_powerGeneration.load();}
ULONGLONG GetTickCount64(){return nowTick;}
void Sleep(DWORD){}
void KillTimer(int,int){}
const char* powerLifecycleName(PowerLifecycle){return "test";}
template<class... T> void traceLog(const char*,T...){}
void sgRecordFact(const char* event,const json& details=json::object()){facts.emplace_back(event,details);}
void appendNativeLifecycleLog(const char*,const json&){}
void appendFanLifecycleLog(const char*,const json&){}
void fanHostScheduleEmergencyResume(ULONGLONG){}
std::vector<std::pair<std::string,json>> emittedEvents;
void ipc_emit(const char* name,const json& details){emittedEvents.emplace_back(name,details);}
bool sgPlatformIsAlwaysOnAlwaysConnected(){return aoac;}
// This PID-only fixture supplies S0 intent explicitly; capability routing is
// tested with real production handlers in the S3/non-user companion replay.
bool sgModernStandbyRepairSupported(){return true;}
bool nativeValveReadCurrentTarget(NativeDetectedGame* target,ULONGLONG* generation){*target=detected; *generation=11; return targetAvailable;}
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
void sgWriteFile(const std::wstring&,const std::string&){}
double sgNowEpoch(){return 1;}
void sgInitNt(){}
bool focusQueryProcessIdentity(DWORD,void*,ULONGLONG* created){*created=actualCreated; return processAlive;}
HANDLE OpenProcess(int,bool,DWORD pid){if(swapOnOpen){actualCreated++;swapOnOpen=false;}return processAlive?pid:0;}
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
 markerReadOffset=0;return 900;
}
bool ReadFile(HANDLE file,void* buffer,DWORD bytes,DWORD* count,void*){
 if(file!=900||!count)return false;
 const size_t available=markerReadBody.size()-markerReadOffset;
 *count=static_cast<DWORD>(std::min<size_t>(bytes,available));
 markerReadBody.copy(static_cast<char*>(buffer),*count,markerReadOffset);markerReadOffset+=*count;return true;
}
bool sgDrainProcessSuspendCount(HANDLE h){assert(h==100); if(resumeFails)return false; ++resumeCalls; freezeDepth=0; return true;}
void focusSingleResumedGame(const std::vector<DWORD>&,ULONGLONG=0,unsigned long long=0){}
namespace fspath { bool exists(const std::wstring&,std::error_code&){return markerExists;} void remove(const std::wstring&,std::error_code&){markerExists=false;} }
SgResumeResult sgResumeMarkedDirectoryUnlocked(const std::wstring& dir,bool){assert(dir==SG_SLEEP_LEASE_DIR); if(markerExists && !resumeFails){markerExists=false;freezeDepth=0;}return {};}
json sgSuspendGameByPidUnlocked(DWORD pid,const std::wstring& dir,ULONGLONG created,const NativeDetectedGame*,bool,ULONGLONG gen){
 assert(pid==100 && dir==SG_SLEEP_LEASE_DIR && created==actualCreated);
 ++suspendCalls; ++freezeDepth; markerExists=true; markerCreated=created; markerGeneration=gen;
 if(pauseHook)pauseHook();
 return {{"paused",true},{"okCount",1},{"failCount",0}};
}
void sgStopSleepRetry(){g_sgRetryInProgress=false;g_sgRetryDueTick=0;g_sgSleepTriggerTick=0;g_sgTask.retryKind=SgRetryKind::None;g_sgTask.retryAttempt=0;cancelEntered=true;}
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
void openHardwareWriteGate(){}
void closeHardwareWriteGate(const char*){}
void inputCaptureResumeSensorsForPower(bool){++sensorCalls;}
void inputHostRequestWakeRescan(ULONGLONG){++rescanCalls;}
void inputHostRequestPowerRelease(ULONGLONG,const char*){++powerReleaseCalls;}
void gamepadResetNativeState(bool){++resetCalls;}
void sgHandheldSleepSensorsOnly(const char*){}
void sgStopMonitorEntryFailureTimer(const char*){}
void fanHostScheduleEmergencySuspend(const char*,ULONGLONG){}
bool sgPowerButtonSleepConfigured(){return true;}
bool sgHibernateAvailable(){return true;}
void sgQueueWork(SgWork work,ULONGLONG gen){queue.emplace_back(work,gen);}
void sgMarkSleepTrigger(){g_sgTask.generation=currentPowerGeneration();g_sgRepairEligible=g_guardEnabled&&g_sgSleepIntentArmed;g_sgSleepCycleActive=g_sgRepairEligible;g_sgGameActuallySuspended=false;g_sgSleepIntentArmed=false;}
void sgClearUserStandby(const char*){++abortCalls;}
void inputHostRequestPowerRearm(ULONGLONG,const char*){}
bool sgS0EntryFailureEligible(int reason,ULONGLONG){return reason==7 || reason==8;}
void sgAdvanceSleepRetry(const char*){}
void sgStartSleepRetry(SgRetryKind kind,const char*){g_sgTask.retryKind=kind;g_sgRetryInProgress=true;}
'''
names = ['sgShouldKeepInputDuringStandby','sgVirtualWriterAllowedNow',
         'sgReadResumeMarkerChecked','sgResumeMarkerDefinitelyStale',
         'sgResumeSleepTargetUnlocked','sgResumeSleepTarget','sgPauseSleepTarget',
         'sgRealWake','sgAbortSleepIntent','sgCancelEntryFailureRetry','sgHandheldResumeInputOnWake','handlePowerResumeNotification',
         'sgBeginModernStandbyIntent','sgHandleModernStandbyWake']
functions = '\n\n'.join(function(name) for name in names)
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
preamble += pure_context
# The real definition inherits this default argument from its forward declaration.
preamble += '\nstatic SgResumeResult sgResumeSleepTarget(unsigned long long, bool = false);\n'
preamble += '''
int queuedGameRecovery=0;
void msiVisibilityObserverNoteResume(unsigned long long){}
void inputHostArmWakePhysicalHold(const char*){}
static void sleepRecoveryAutoArm(unsigned long long){} // automatic owner separately tested; this fixture keeps opt-in off
static void sgBeginWakeGameRecovery(unsigned long long generation){assert(generation==7);++queuedGameRecovery;}
static void sgPrepareWakeRecoveryForSleep(unsigned long long){
 // Boundary fake: production reconciliation is compiled in the wake-header suite.
 // Here it exposes dispatch ordering, including flags written before capture.
 const bool owned=g_sgSleepTarget.markerOwned || markerExists;
 g_sgInSuspend=owned;g_sgGameActuallySuspended=owned;
}
'''

checks = r'''
void reset(){
 g_powerLifecycle=PowerLifecycle::Suspending;g_powerGeneration=7;g_resumeReadyGeneration=0;
 g_sensorSuspendCallbackActive=false;g_sgLastKernel506EventFileTime=0;g_sgLastKernel507EventFileTime=0;g_sgInternalSleepRequest={};g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::None;
 g_sgTask={};g_sgInSuspend=false;g_sgGameActuallySuspended=false;g_sgSleepCycleActive=true;
 g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_wakeHoldPhysicalUnhide=false;
 g_guardEnabled=true;g_sgPauseResume=true;g_sgRepairEligible=true;g_sgRetryInProgress=false;
 g_sgModernStandbyActive=true;g_sgModernWakeClassified=false;g_sgModernWakeClassifiedTick=0;
 g_sgLastModernStandbyIntentTick=0;g_lastResumeNotifyTick=0;g_sgSleepIntentArmed=false;
 g_sgSleepQueryGateClosed=false;g_sgSleepQueryOwnsGeneration=false;g_sgSleepQueryGeneration=0;
 g_machineIdentity.family=YmccFamily::Handheld;aoac=false;targetAvailable=true;manualLease=false;
 markerExists=false;resumeFails=false;swapOnOpen=false;processAlive=true;actualCreated=1234;markerCreated=1234;markerReadBody.clear();markerReadOffset=0;
 markerGeneration=7;nowTick=10000;g_sgSleepTarget={};detected={};queue.clear();facts.clear();pauseHook={};cancelEntered=false;
 emittedEvents.clear();oemWakePortCalls.clear();
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
 g_powerLifecycle=PowerLifecycle::Ready;g_resumeReadyGeneration=7;sgHandleModernStandbyWake(true);
 assert(queue.back().first==SgWork::WakeSuspend&&queue.back().second==7);sgRealWake("resume_suspend",7);assert(queuedGameRecovery==1&&resumeCalls==0);sgResumeSleepTarget(7,false);
 assert(resumeCalls==1&&freezeDepth==0);
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
 reset();freeze();auto wrong=sgResumeSleepTarget(6,false);
 assert(wrong.count==0&&freezeDepth==1&&markerExists);
 pass("wrong power generation cannot resume a valid lease");
 reset();freeze();resumeFails=true;auto failed=sgResumeSleepTarget(7,false);
 assert(failed.count==0&&freezeDepth==1&&markerExists&&g_sgSleepTarget.markerOwned);
 pass("failed resume retains owned marker for recovery");
 reset();freeze();manualLease=true;sgRealWake("resume_suspend",7);assert(queuedGameRecovery==1&&resumeCalls==0);sgResumeSleepTarget(7,false);
 assert(freezeDepth==0&&resumeCalls==1&&manualLease);
 pass("explicit wake queues recovery without inline thaw; legacy exact-lease helper stays separate (manual batch covered by wake suite)");
 reset();freeze();g_sensorSuspendCallbackActive=true;sgAbortSleepIntent("user-cancel");
 assert(resumeCalls==1&&freezeDepth==0&&!markerExists&&g_powerLifecycle==PowerLifecycle::Ready&&!g_sensorSuspendCallbackActive);
 pass("canceled sleep recovers only its exact lease");
 reset();std::thread cancelThread;
 pauseHook=[&]{
  cancelThread=std::thread([]{sgAbortSleepIntent("cancel-during-freeze");});
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
functions += '\nvoid runProductionSuspendPause(ULONGLONG generation){\nstruct { ULONGLONG generation; } item{generation};\n' + worker_pause_gate + '\n}\n'
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
 sgAbortSleepIntent("episode-context");
 assert(SleepSteamRecovery::repeatedSuspendGeneration==0&&SleepSteamRecovery::repeatedSuspendEpoch==0&&g_powerLifecycle==PowerLifecycle::Ready);
 pass("cancel closes the exact extracted pure suspend episode");
 reset();g_fanPhysicalWake.recordSleep(7,100);g_fanPhysicalWake.recordSleep(7,200);
 const auto fanGeneration=currentFanPowerGeneration();assert(fanGeneration!=currentPowerGeneration());
 sgAbortSleepIntent("fan-generation-context");
 assert(currentPowerGeneration()==7&&emittedEvents.size()==1&&emittedEvents[0].first=="fan.resume-ready"&&emittedEvents[0].second.at("generation").get<unsigned long long>()==fanGeneration);
 pass("cancel emits the actual pure fan generation without a second shared transaction");
 reset();sgHandheldResumeInputOnWake("oem-memory-port");
 assert(oemWakePortCalls.size()==1&&oemWakePortCalls[0].generation==7&&oemWakePortCalls[0].reason=="oem-memory-port"&&oemWakePortCalls[0].sensorsBefore==0&&sensorCalls==1&&resumeCalls==0);
 pass("input wake supplies generation and reason to counted OEM boundary before sensors");
 std::cout<<"native sleep-game pause selftest:''')
(out/'source-bindings.json').write_text(report_json.dumps({
 'sourceSha256':hashlib.sha256(args.source.read_bytes()).hexdigest(),
 'scriptSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest(),
 'extractedDefinitions':extracted,
 'actualMainConsumerFunctions':names,
 'additionalPureMainFunction':'currentFanPowerGeneration',
 'additionalPureHeaderFunctions':['SleepSteamRecovery::closeSuspendEpisode','sleepRecoveryAutoCloseSuspendEpisode'],
 'sourceOnlySuspendHelper':'sgSuspendGameByPidUnlocked',
 'originalNativeAssertionStatements':48,'originalPassGroups':24,
 'newFamilySeparated':True,'previousFamilyNames':previous_family_names,'newDesktopFamily':'ValveSteamMachine',
 'ownCancellationThread':{'waitMs':25,'joined':True},
 'oemPort':'counted generation/reason/order only; no OEM production scheduler',
 'realProcessOrSleepOperations':False
},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
cpp=out/'sleep_game_pause_native_selftest.cpp'
cpp.write_text(preamble+'\n'+functions+'\n'+checks,encoding='utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
assert vs.is_file(), 'MSVC build environment not found'
repo=Path.cwd()
cmd=out/'run-native-selftest.cmd'
exe=out/'sleep_game_pause_native_selftest.exe'
toolchain_js='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain_prefix=subprocess.check_output(['node','--input-type=module','-e',toolchain_js,str(vs)],text=True,encoding='utf-8')
cmd.write_text(toolchain_prefix+f'if errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{cpp}" /I"{repo / "deps/json"}" /I"{repo / "native"}" /Fo"{out / "sleep_game_pause_native_selftest.obj"}" /Fe"{exe}"\r\nif errorlevel 1 exit /b 1\r\n"{exe}"\r\n',encoding='ascii')
result=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'native-selftest.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8',errors='replace'))
raise SystemExit(result.returncode)
