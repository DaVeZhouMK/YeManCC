"""Replay USB4 failures through production C++ functions, with OS sleep mocked.

The test extracts the actual wake handlers and retry functions from main.cpp.
It never sleeps this computer or suspends a user process. Run from repo root.
"""
from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=Path('native/main.cpp'))
parser.add_argument('--output', type=Path, default=Path('../../Build/Validation/SleepNonUserWake'))
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8-sig')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                lambda m: ' ' * len(m.group()), source, flags=re.S)


def braced(opening):
    end, depth = opening + 1, 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return end


def function(name):
    match = re.search(r'\bstatic\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*\{', masked)
    assert match, f'missing function {name}'
    opening = masked.index('{', match.start())
    return source[match.start():braced(opening)]


def body_after(anchor):
    start = source.index(anchor)
    opening = masked.index('{', start)
    return source[opening + 1:braced(opening) - 1]


def case_body(start, end):
    block = source[source.index(start):source.index(end, source.index(start))]
    opening = block.index('{')
    return block[opening + 1:block.rfind('}')]


assert 'SG_USER_STANDBY_DEVICE_DELAY_MS = 120000ULL' in source
assert 'SG_NON_USER_WAKE_CLASSIFY_MS = 2000ULL' in source
for broadcast in ['PBT_APMRESUMEAUTOMATIC', 'PBT_APMRESUMESUSPEND']:
    body = body_after('else if (w == ' + broadcast + ')')
    assert 'sgObserveNonUserWake(' in body
    if 'sgClearUnexpectedWake(' in body:
        assert body.index('sgObserveNonUserWake(') < body.index('sgClearUnexpectedWake(')
assert 'sgArmNonUserWakeGuard("sleep-trigger")' in function('sgMarkSleepTrigger')
assert 'SetSuspendState(FALSE, FALSE, FALSE)' in function('sgRequestSystemSleep')
assert 'g_sgPauseResume' not in function('sgEvaluateExternalDeviceWake')

names = [
    'sgSleepRetryActive', 'sgMarkInternalSleepRequest', 'sgClearInternalSleepRequest',
    'sgInternalSleepRequestMatches506', 'sgNonUserWakeGuardEnabled',
    'sgExternalDeviceWakeIntentAge', 'sgClearUnexpectedWake', 'sgArmNonUserWakeGuard',
    'sgEvaluateExternalDeviceWake', 'sgObserveNonUserWake',
    'sgNoteExternalDeviceNodeChange', 'sgNoteExternalDeviceAcDcChange',
    'sgNoteExternalDeviceKernel507Reason5', 'sgNoteExternalDeviceKernel507Reason7',
    'sgMarkSleepTrigger', 'sgStopSleepRetry', 'sgRealWake',
    'sgFinishSleepRetryFailure', 'sgScheduleNextSleepRetry', 'sgDispatchSameModeRetry',
    'sgStartSleepRetry', 'sgAdvanceSleepRetry', 'sgS0EntryFailureEligible',
    'sgKernelPowerEventFileTime',
]
functions = '\n\n'.join(function(name) for name in names)
handlers = '\n'.join([
    'int handle506(WPARAM w, LPARAM l) {\n' + case_body('case WM_SG_S0_INTENT:', 'case WM_SG_S0_WAKE:') + '\n}',
    'int handle507(WPARAM w, LPARAM l) {\n' + case_body('case WM_SG_S0_WAKE:', 'case WM_SG_S4_WAKE:') + '\n}',
    'int resumeAutomatic() {\n' + body_after('else if (w == PBT_APMRESUMEAUTOMATIC)') + '\nreturn TRUE;\n}',
    'int resumeInteractive() {\n' + body_after('else if (w == PBT_APMRESUMESUSPEND)') + '\nreturn TRUE;\n}',
])
suspend = body_after('else if (w == PBT_APMSUSPEND)')
# Execute the production retry-confirmation branch; non-retry setup is covered
# by the production sgMarkSleepTrigger and the single-PID companion selftest.
retry_start = suspend.index('if (sgSleepRetryActive())')
retry_open = suspend.index('{', retry_start)
retry_mask = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', lambda m: ' ' * len(m[0]), suspend, flags=re.S)
depth, retry_end = 1, retry_open + 1
while depth:
    depth += (retry_mask[retry_end] == '{') - (retry_mask[retry_end] == '}')
    retry_end += 1
handlers += '\nint confirmRetrySuspend() {\n' + suspend[retry_start:retry_end] + '\nreturn FALSE;\n}\n'

preamble = r'''
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>
#include "json.hpp"
using json = nlohmann::json;
#define GetTickCount64 fakeGetTickCount64
#define SetTimer fakeSetTimer
#define KillTimer fakeKillTimer
static constexpr UINT_PTR SG_RETRY_TIMER_ID=0xA20C;
static constexpr ULONGLONG SG_USER_STANDBY_DEVICE_DELAY_MS=120000, SG_NON_USER_WAKE_CLASSIFY_MS=2000, SG_PAUSE_READY_WAIT_MAX_MS=2000;
static constexpr UINT SG_PAUSE_READY_POLL_MS=50;
static constexpr ULONGLONG SG_ENTRY_RETRY_DELAYS_MS[]={500,1000,2000};
static constexpr unsigned SG_MAX_ENTRY_RETRIES=3;
static constexpr ULONGLONG SG_INTERNAL_SLEEP_506_WINDOW_MS=60000, SG_INTERNAL_SLEEP_506_CLOCK_SKEW_MS=2000;
static constexpr ULONGLONG SG_S0_REASON7_FAILURE_WINDOW_MS=5000;
static constexpr ULONGLONG epoch=133000000000000000ULL;
enum class PowerLifecycle : uint8_t {Ready,Suspending,Suspended,Resuming};
enum class SgSleepMode : uint8_t {Unknown,S3,S4};
enum class SgRetryKind : uint8_t {None,EntryFailure,UnexpectedWake};
enum class SgWork : uint8_t {Suspend,WakeAutomatic,WakeSuspend,WakeHibernate};
struct SgSleepTask {unsigned long long generation=0;SgSleepMode mode=SgSleepMode::Unknown;SgRetryKind retryKind=SgRetryKind::None;unsigned retryAttempt=0;bool unexpectedWakeConsumed=false;} g_sgTask;
struct Internal {SgRetryKind kind=SgRetryKind::None;ULONGLONG dispatchTick=0,dispatchFileTime=0;} g_sgInternalSleepRequest;
struct SgSleepTarget {bool markerOwned=false;unsigned long long powerGeneration=0;};
struct SgResumeResult {int count=0;};
struct SgSystemSleepRequestResult {bool accepted=true,shutdownPrivilegeEnabled=true;DWORD shutdownPrivilegeError=0,requestError=0;};
HWND g_hwnd=reinterpret_cast<HWND>(1);
ULONGLONG nowTick=10000, nowFile=epoch+10000*10000ULL, generation=2, timerDue=0;
ULONGLONG g_sgIgnoredEarlyWakeFileTime=0,g_sgRetryDueTick=0,g_sgSleepTriggerTick=0,g_sgLastS0WakeTick=0,g_sgLastPowerButtonWakeTick=0,g_sgModernStandbyGeneration=0,g_sgModernWakeClassifiedTick=0,g_lastResumeNotifyTick=0;
double g_sgSleepTriggerEpoch=0;
std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{0},g_sgLastPowerButtonWakeEventFileTime{0},g_sgPauseWorkCompletedGeneration{0};
std::atomic<int> g_sgLastKernel506Reason{-1};
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
std::atomic<bool> g_sgSleepCycleActive{false},g_sgGameActuallySuspended{false},g_sgInSuspend{false},g_inputReady{true},g_inputHostPowerAdmissionRevoked{false};
bool g_guardEnabled=true,g_sgResleepEnabled=true,g_sgRetryEntryFailure=true,g_sgRepairEligible=false,g_sgRetryInProgress=false,g_sgNonUserWakeObserved=false,g_sgSleepIntentArmed=false,g_sgModernStandbyActive=false,g_sgModernWakeClassified=false;
bool g_sgSleepQueryGateClosed=false,g_sgSleepQueryOwnsGeneration=false;
unsigned long long g_sgSleepQueryGeneration=0;
int g_sgLastS0WakeReason=-1;
bool requestAccepted=true,keepInput=true,frozen=false;
int requestCount=0,resumeCount=0,beginCount=0,abortCount=0;
std::vector<std::pair<std::string,json>> facts;
const std::wstring SG_SLEEP_TRIGGER_MARKER=L"mock";
ULONGLONG fakeGetTickCount64(){return nowTick;}
ULONGLONG sgNowFileTime(){return nowFile;}
double sgNowEpoch(){return double(nowFile)/10000000;}
ULONGLONG currentPowerGeneration(){return generation;}
UINT_PTR fakeSetTimer(HWND,UINT_PTR id,UINT delay,TIMERPROC){assert(id==SG_RETRY_TIMER_ID);timerDue=nowTick+delay;return id;}
BOOL fakeKillTimer(HWND,UINT_PTR id){assert(id==SG_RETRY_TIMER_ID);timerDue=0;return TRUE;}
void sgRecordFact(const char* e,const json& d=json::object()){facts.emplace_back(e,d);}
const char* sgRetryKindName(SgRetryKind kind){return kind==SgRetryKind::EntryFailure?"entry-failure":kind==SgRetryKind::UnexpectedWake?"unexpected-wake":"none";}
std::string sgFormatFactFileTime(ULONGLONG t){return std::to_string(t);}
const char* sgKernelPowerReasonName(int){return "test";}
const char* powerLifecycleName(PowerLifecycle){return "test";}
template<class... T> void traceLog(const char*,T...){}
void stopPowerResumeWatchdog(){}
void armPowerResumeWatchdog(ULONGLONG){}
void closeHardwareWriteGate(const char*){}
void openHardwareWriteGate(){}
void closeJoyXoffAsync(const char*){}
void sgWriteFileAtomic(const std::wstring&,const std::string&){}
void sgStopMonitorEntryFailureTimer(const char*){}
void inputHostRequestPowerRelease(ULONGLONG,const char*){}
void ipc_emit(const char*,const json&){}
bool sgShouldKeepInputDuringStandby(){return keepInput;}
bool sgReadSleepTarget(SgSleepTarget& target){target={frozen,generation};return frozen;}
bool sgSleepLeaseOwnedByGeneration(ULONGLONG g){return frozen&&g==generation;}
SgResumeResult sgResumeSleepTarget(ULONGLONG g,bool){assert(g==generation);if(frozen){frozen=false;++resumeCount;}return {resumeCount};}
void sgBeginWakeGameRecovery(ULONGLONG g){(void)sgResumeSleepTarget(g,true);}
SgSystemSleepRequestResult sgRequestSystemSleep(){++requestCount;if(requestAccepted)timerDue=0;return {requestAccepted,true,0,requestAccepted?0UL:5UL};}
json sgSleepEnvironmentSnapshot(){return json::object();}
static bool sgSleepRetryActive();
static void sgStartSleepRetry(SgRetryKind,const char*);
static void sgScheduleNextSleepRetry(const char*);
static void sgFinishSleepRetryFailure(const char*);
static void sgStopSleepRetry();
static void sgMarkSleepTrigger();
static void sgRealWake(const char*,unsigned long long);
static void sgClearUnexpectedWake(const char*,bool);
static bool sgS0EntryFailureEligible(int,ULONGLONG);
static void sgAdvanceSleepRetry(const char*);
void sgQueueWork(SgWork w,ULONGLONG g){if(w==SgWork::WakeSuspend)sgRealWake("resume_suspend",g);}
void sgAbortSleepIntent(const char*){++abortCount;sgStopSleepRetry();sgRealWake("resume_suspend",generation);g_powerLifecycle=PowerLifecycle::Ready;}
void sgMarkUserStandby(const char*,int,ULONGLONG){}
void sgBeginModernStandbyIntent(){
 ++beginCount;
 if(g_sgTask.mode==SgSleepMode::Unknown){g_sgTask.mode=SgSleepMode::S3;g_sgSleepIntentArmed=true;sgMarkSleepTrigger();g_powerLifecycle=PowerLifecycle::Suspended;}
 else g_sgModernStandbyActive=true;
}
void handlePowerResumeNotification(SgWork w,const char*){
 if(sgSleepRetryActive()&&w!=SgWork::WakeSuspend)return;
 sgRealWake(w==SgWork::WakeSuspend?"resume_suspend":"resume_auto",generation);
 g_powerLifecycle=PowerLifecycle::Ready;
}
void sgHandleModernStandbyWake(bool user){
 if(user){sgRealWake("resume_suspend",generation);g_powerLifecycle=PowerLifecycle::Ready;return;}
 if(sgS0EntryFailureEligible(g_sgLastS0WakeReason,nowTick)){
  if(sgSleepRetryActive())sgAdvanceSleepRetry("entry-failure");
  else if(g_sgRetryEntryFailure)sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-failure");
 }
}
'''
checks = r'''
void at(ULONGLONG tick){nowTick=tick;nowFile=epoch+tick*10000;}
void reset(bool pause=false){
 at(10000);generation=2;timerDue=0;g_sgTask={};g_sgInternalSleepRequest={};
 g_sgLastPowerButtonSleepIntentFileTime=0;g_sgLastPowerButtonWakeEventFileTime=0;g_sgPauseWorkCompletedGeneration=2;
 g_guardEnabled=true;g_sgResleepEnabled=true;g_sgRetryEntryFailure=true;g_sgRepairEligible=false;
 g_sgRetryInProgress=false;g_sgNonUserWakeObserved=false;g_sgIgnoredEarlyWakeFileTime=0;g_sgSleepIntentArmed=false;
 g_sgModernStandbyActive=false;g_sgModernWakeClassified=false;g_sgModernWakeClassifiedTick=0;
 g_sgSleepCycleActive=false;g_sgGameActuallySuspended=false;g_sgInSuspend=false;
 g_powerLifecycle=PowerLifecycle::Ready;g_sgRetryDueTick=0;g_sgSleepTriggerTick=0;
 requestAccepted=true;keepInput=true;frozen=pause;requestCount=resumeCount=beginCount=abortCount=0;facts.clear();
}
void sleepIntent(){
 g_sgTask.generation=generation;g_sgTask.mode=SgSleepMode::S3;g_sgSleepIntentArmed=true;
 g_powerLifecycle=PowerLifecycle::Suspending;sgMarkSleepTrigger();g_powerLifecycle=PowerLifecycle::Suspended;
 g_sgInSuspend=frozen;g_sgGameActuallySuspended=frozen;g_sgPauseWorkCompletedGeneration=generation;
}
void dispatch(){assert(timerDue);at(timerDue);sgDispatchSameModeRetry();}
void powerButton(){g_sgLastPowerButtonWakeEventFileTime=nowFile;handle507(1,1);}
int passed=0;
void pass(const char* label){++passed;std::cout<<"PASS "<<label<<"\n";}
void advance(ULONGLONG tick){
 unsigned loops=0;
 while(timerDue&&timerDue<tick){assert(++loops<8);dispatch();}
 at(tick);
}
void replay(const char* path){
 std::ifstream f(path);json fixture;f>>fixture;reset();
 for(const auto& e:fixture["events"]){
  advance(e["atMs"].get<ULONGLONG>());std::string kind=e["kind"];
  if(kind=="broadcast"){
   int code=e["code"];
   if(code==4){if(!confirmRetrySuspend())sleepIntent();}
   else if(code==18)resumeAutomatic();else if(code==7)resumeInteractive();
  }else if(kind=="kernel"){
   int id=e["id"],reason=e["reason"];
   if(id==506){g_sgLastKernel506Reason=reason;handle506(reason,epoch+e["eventAtMs"].get<ULONGLONG>()*10000);}
   else{if(reason==1)g_sgLastPowerButtonWakeEventFileTime=epoch+e["eventAtMs"].get<ULONGLONG>()*10000;handle507(reason==1?1:2,reason);}
  }else if(kind=="device")sgNoteExternalDeviceNodeChange();else sgNoteExternalDeviceAcDcChange();
 }
 advance(nowTick+10000);
 assert(requestCount==fixture["expectedWakeCycles"].get<int>());
 assert(resumeCount==0);
 std::cout<<"PASS fixture "<<path<<": "<<requestCount<<" guarded USB4 wake cycles submit SetSuspendState (mocked)\n";++passed;
}
int main(int argc,char** argv){
 std::cout<<std::unitbuf;
 for(ULONGLONG age:{0ULL,6000ULL,14000ULL,119999ULL}){
  reset();sleepIntent();at(10000+age);resumeAutomatic();resumeInteractive();handle507(2,5);
  sgNoteExternalDeviceNodeChange();assert(!timerDue&&!requestCount&&!sgSleepRetryActive());
 }
 pass("USB4 wakes before 120 seconds never submit or defer re-sleep");
 for(bool pause:{false,true}){
  reset(pause);sleepIntent();at(130000);resumeAutomatic();resumeInteractive();handle507(2,5);
  for(int i=0;i<100;++i)sgNoteExternalDeviceNodeChange();
  assert(sgSleepRetryActive()&&g_sgTask.retryKind==SgRetryKind::UnexpectedWake&&requestCount==0&&resumeCount==0);
  assert(timerDue==132000);dispatch();assert(requestCount==1&&resumeCount==0);
  pass(pause?"USB4 at 120 seconds repairs with game lease held":"USB4 at 120 seconds repairs without a game");
 }
 reset();sleepIntent();at(130001);resumeAutomatic();dispatch();assert(requestCount==1);
 pass("USB4 after 120 seconds repairs normally");
 reset();sleepIntent();at(16000);resumeAutomatic();int began=beginCount;
 handle506(1,epoch+9000*10000ULL);assert(beginCount==began&&!timerDue);
 at(150000);handle507(2,5);sgNoteExternalDeviceNodeChange();assert(!timerDue&&!requestCount);
 pass("early wake and its delayed 506 cannot trigger a late retry after 120 seconds");
 reset();for(int i=0;i<100;++i){sgNoteExternalDeviceNodeChange();sgNoteExternalDeviceAcDcChange();}resumeInteractive();
 assert(!timerDue&&!requestCount);pass("awake USB/charger insertion cannot trigger sleep");
 reset();sleepIntent();at(140000);sgNoteExternalDeviceAcDcChange();sgNoteExternalDeviceNodeChange();assert(!timerDue);
 pass("sleep-entry enumeration and charger changes alone are not a wake");
 for(int reason:{5,7,8,25,28,55,16777220,-1}){
  reset();sleepIntent();at(140000);handle507(2,reason);assert(sgSleepRetryActive());dispatch();assert(requestCount==1);
 }
 pass("all non-power-button reasons repair after 120 seconds, not an AMD-only whitelist");
 reset(true);sleepIntent();at(140000);resumeAutomatic();resumeInteractive();at(141000);powerButton();advance(150000);
 assert(!requestCount&&resumeCount==1&&!sgSleepRetryActive());sgNoteExternalDeviceNodeChange();assert(!timerDue);
 pass("physical button cancels pending USB4 retry and recovers exact lease");
 reset(true);sleepIntent();at(140000);resumeAutomatic();g_sgLastPowerButtonWakeEventFileTime=nowFile+10000;dispatch();
 assert(!requestCount&&!sgSleepRetryActive()&&resumeCount==1);
 pass("callback-side button evidence wins even before UI message dispatch");
 reset();sleepIntent();at(140000);resumeAutomatic();int beganLong=beginCount;
 handle506(1,epoch+9000*10000ULL);assert(beginCount==beganLong&&sgSleepRetryActive());dispatch();assert(requestCount==1);
 pass("delayed old 506 cannot rebuild generation or erase eligible scheduled retry");
 reset();sleepIntent();at(140000);powerButton();int b=beginCount;handle506(1,epoch+10000*10000ULL);
 assert(!g_sgLastPowerButtonSleepIntentFileTime.load()&&beginCount==b);sgNoteExternalDeviceNodeChange();assert(!timerDue);
 pass("stale 506 after button wake cannot re-arm sleep guard");
 reset();sleepIntent();at(140000);resumeAutomatic();handle507(2,5);dispatch();assert(requestCount==1);assert(confirmRetrySuspend());
 at(143000);handle506(3,g_sgInternalSleepRequest.dispatchFileTime);assert(beginCount==0);
 at(170000);resumeAutomatic();dispatch();assert(requestCount==2);
 pass("successful re-sleep retains original eligible intent; internal 506 stays internal");
 reset();sleepIntent();g_sgTask={};g_sgRepairEligible=false;g_powerLifecycle=PowerLifecycle::Ready;
 at(140000);resumeAutomatic();dispatch();assert(requestCount==1&&g_sgTask.generation==generation);
 pass("UI/PID lifecycle already Ready does not lose eligible protected sleep intent");
 reset(true);sleepIntent();g_sgResleepEnabled=false;at(140000);resumeInteractive();assert(!timerDue&&resumeCount==1);
 pass("disabled button leaves normal interactive wake unchanged");
 reset(true);sleepIntent();at(140000);resumeAutomatic();g_sgResleepEnabled=false;dispatch();assert(!requestCount&&resumeCount==1);
 pass("disabling during classification cancels without sleeping");
 reset();g_sgTask.mode=SgSleepMode::S4;sgArmNonUserWakeGuard("s4");resumeAutomatic();assert(!timerDue&&!requestCount);
 pass("hibernate is excluded");
 reset(true);sleepIntent();at(140000);resumeAutomatic();handle507(2,5);requestAccepted=false;dispatch();dispatch();dispatch();
 assert(requestCount==3&&!timerDue&&!sgSleepRetryActive()&&resumeCount==1&&!g_sgLastPowerButtonSleepIntentFileTime.load());
 sgNoteExternalDeviceNodeChange();assert(!timerDue);pass("three rejected API requests stop and recover, no endless retry");
 reset();sleepIntent();nowFile=epoch;ULONGLONG age=1;assert(!sgExternalDeviceWakeIntentAge(age)&&age==0);
 pass("wall-clock rollback cannot bypass the 120-second gate");
 reset();g_guardEnabled=false;resumeAutomatic();handle507(2,5);sgNoteExternalDeviceNodeChange();assert(!timerDue&&!requestCount);
 pass("master sleep optimization disabled has no non-user retry");
 reset();handle506(1,epoch+10000*10000ULL);g_sgLastKernel506Reason=1;g_sgRetryEntryFailure=false;at(11000);handle507(2,7);
 assert(!timerDue&&!requestCount);
 pass("non-user toggle cannot bypass 120 seconds when entry-failure retry is disabled");
 reset();g_sgRetryEntryFailure=false;sleepIntent();at(140000);handle507(2,5);dispatch();assert(requestCount==1);
 pass("eligible non-user wake repair is independent of entry-failure toggle");
 reset();handle506(1,epoch+10000*10000ULL);g_sgLastKernel506Reason=1;at(11000);handle507(2,7);
 assert(g_sgTask.retryKind==SgRetryKind::EntryFailure);pass("separate rapid S0 entry-failure feature remains unchanged");
 ULONGLONG second=sgKernelPowerEventFileTime("<TimeCreated SystemTime='2026-09-30T15:14:50Z'/>");
 assert(sgKernelPowerEventFileTime("<TimeCreated SystemTime='2026-09-30T15:14:50.1234567Z'/>")-second==1234567);
 assert(sgKernelPowerEventFileTime("<TimeCreated SystemTime=\"2026-09-30T15:14:50.5Z\"/>")-second==5000000);
 pass("power event parser preserves sub-second button/sleep ordering");
 for(int i=1;i<argc;++i)replay(argv[i]);
 std::cout<<"non-user wake native selftest: "<<passed<<" groups PASS (120-second gate; actual production functions; OS/PID boundaries mocked)\n";
}
'''
cpp = out / 'sleep_non_user_wake_native_selftest.cpp'
cpp.write_text(preamble + '\n' + functions + '\n' + handlers + '\n' + checks, encoding='utf-8')
repo = Path.cwd()
vs = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
assert vs.is_file(), 'MSVC environment not found'
exe = out / 'sleep_non_user_wake_native_selftest.exe'
fixtures = [repo / 'tools/fixtures/sleep_non_user_wake' / name for name in
            ['amd_usb4_failure.json', 'intel_usb4_failure.json', 'amd_power_button_wake.json']]
cmd = out / 'run-native-selftest.cmd'
cmd.write_text(f'@echo off\r\ncall "{vs}" >nul\r\nif errorlevel 1 exit /b 1\r\n'
               f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /D_CRT_SECURE_NO_WARNINGS "{cpp}" /I"{repo / "deps/json"}" '
               f'/Fo"{out / "sleep_non_user_wake_native_selftest.obj"}" /Fe"{exe}"\r\n'
               f'if errorlevel 1 exit /b 1\r\n"{exe}" ' + ' '.join(f'"{p}"' for p in fixtures) + '\r\n', encoding='ascii')
result = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(out / 'native-selftest.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8', errors='replace'))
raise SystemExit(result.returncode)
