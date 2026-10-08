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

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=Path('native/main.cpp'))
parser.add_argument('--output', type=Path, default=Path('../../Build/Validation/SleepGamePause-20260929'))
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8-sig')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)

# Mask comments/strings without changing offsets before counting C++ braces.
masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                lambda m: ' ' * len(m.group()), source, flags=re.S)

def function(name):
    match = re.search(r'\bstatic\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*\{', masked)
    assert match, f'missing native function: {name}'
    opening = masked.index('{', match.start())
    depth = 1
    end = opening + 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return source[match.start():end]

def section(start, end):
    begin = source.index(start)
    return source[begin:source.index(end, begin + len(start))]

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
worker_pause = section('if (item.kind == SgWork::Suspend)', '} else if (item.kind == SgWork::WakeAutomatic)')
assert 'sgShouldKeepInputDuringStandby' not in worker_pause
assert 'const SgSuspendResult result = sgPauseSleepTarget(item.generation);' in worker_pause
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
#include <utility>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
using DWORD = unsigned long; using ULONGLONG = unsigned long long;
using HANDLE = unsigned long;
constexpr bool FALSE = false;
constexpr int PROCESS_SUSPEND_RESUME = 1, PROCESS_QUERY_LIMITED_INFORMATION = 2;
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
ULONGLONG actualCreated=1234, markerGeneration=7, nowTick=10000;
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
void ipc_emit(const char*,const json&){}
bool sgPlatformIsAlwaysOnAlwaysConnected(){return aoac;}
bool nativeValveReadCurrentTarget(NativeDetectedGame* target,ULONGLONG* generation){*target=detected; *generation=11; return targetAvailable;}
bool nativeValveValidateOwnedSuspendedTarget(const std::wstring& dir,DWORD pid,ULONGLONG created){
 if(dir==SG_MANUAL_DIR)return manualLease;
 return markerExists && pid==100 && created==actualCreated;
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
bool sgDrainProcessSuspendCount(HANDLE h){assert(h==100); if(resumeFails)return false; ++resumeCalls; freezeDepth=0; return true;}
void focusSingleResumedGame(const std::vector<DWORD>&,ULONGLONG=0,unsigned long long=0){}
namespace fspath { bool exists(const std::wstring&,std::error_code&){return markerExists;} void remove(const std::wstring&,std::error_code&){markerExists=false;} }
SgResumeResult sgResumeMarkedDirectoryUnlocked(const std::wstring& dir,bool){assert(dir==SG_SLEEP_LEASE_DIR); if(markerExists && !resumeFails){markerExists=false;freezeDepth=0;}return {};}
json sgSuspendGameByPidUnlocked(DWORD pid,const std::wstring& dir,ULONGLONG created,const NativeDetectedGame*,bool,ULONGLONG gen){
 assert(pid==100 && dir==SG_SLEEP_LEASE_DIR && created==actualCreated);
 ++suspendCalls; ++freezeDepth; markerExists=true; markerGeneration=gen;
 if(pauseHook)pauseHook();
 return {{"paused",true},{"okCount",1},{"failCount",0}};
}
void sgStopSleepRetry(){g_sgRetryInProgress=false; g_sgTask.retryKind=SgRetryKind::None;cancelEntered=true;}
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
         'sgResumeSleepTargetUnlocked','sgResumeSleepTarget','sgPauseSleepTarget',
         'sgRealWake','sgAbortSleepIntent','sgHandheldResumeInputOnWake','handlePowerResumeNotification',
         'sgBeginModernStandbyIntent','sgHandleModernStandbyWake']
functions = '\n\n'.join(function(name) for name in names)
# The real definition inherits this default argument from its forward declaration.
preamble += '\nstatic SgResumeResult sgResumeSleepTarget(unsigned long long, bool = false);\n'
preamble += '''
int queuedGameRecovery=0;
static void sgBeginWakeGameRecovery(unsigned long long generation){assert(generation==7);++queuedGameRecovery;}
static void sgPrepareWakeRecoveryForSleep(unsigned long long){}
'''

checks = r'''
void reset(){
 g_powerLifecycle=PowerLifecycle::Suspending;g_powerGeneration=7;g_resumeReadyGeneration=0;
 g_sgTask={};g_sgInSuspend=false;g_sgGameActuallySuspended=false;g_sgSleepCycleActive=true;
 g_inputReady=true;g_inputHostPowerAdmissionRevoked=false;g_wakeHoldPhysicalUnhide=false;
 g_guardEnabled=true;g_sgPauseResume=true;g_sgRepairEligible=true;g_sgRetryInProgress=false;
 g_sgModernStandbyActive=true;g_sgModernWakeClassified=false;g_sgModernWakeClassifiedTick=0;
 g_sgLastModernStandbyIntentTick=0;g_lastResumeNotifyTick=0;g_sgSleepIntentArmed=false;
 g_sgSleepQueryGateClosed=false;g_sgSleepQueryOwnsGeneration=false;g_sgSleepQueryGeneration=0;
 g_machineIdentity.family=YmccFamily::Handheld;aoac=false;targetAvailable=true;manualLease=false;
 markerExists=false;resumeFails=false;swapOnOpen=false;processAlive=true;actualCreated=1234;
 markerGeneration=7;nowTick=10000;g_sgSleepTarget={};detected={};queue.clear();facts.clear();pauseHook={};cancelEntered=false;
 queuedGameRecovery=0;freezeDepth=suspendCalls=resumeCalls=powerReleaseCalls=resetCalls=sensorCalls=rescanCalls=abortCalls=0;
}
void freeze(){auto r=sgPauseSleepTarget(currentPowerGeneration());assert(r.frozen&&r.pid==100);g_sgInSuspend=true;g_sgGameActuallySuspended=true;}
int main(){
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
 reset();freeze();sgAbortSleepIntent("user-cancel");
 assert(resumeCalls==1&&freezeDepth==0&&!markerExists&&g_powerLifecycle==PowerLifecycle::Ready);
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
cpp=out/'sleep_game_pause_native_selftest.cpp'
cpp.write_text(preamble+'\n'+functions+'\n'+checks,encoding='utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
assert vs.is_file(), 'MSVC build environment not found'
repo=Path.cwd()
cmd=out/'run-native-selftest.cmd'
exe=out/'sleep_game_pause_native_selftest.exe'
cmd.write_text(f'@echo off\r\ncall "{vs}" >nul\r\nif errorlevel 1 exit /b 1\r\ncl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{cpp}" /I"{repo / "deps/json"}" /Fo"{out / "sleep_game_pause_native_selftest.obj"}" /Fe"{exe}"\r\nif errorlevel 1 exit /b 1\r\n"{exe}"\r\n',encoding='ascii')
result=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'native-selftest.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8',errors='replace'))
raise SystemExit(result.returncode)
