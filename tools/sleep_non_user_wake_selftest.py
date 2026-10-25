"""Replay USB4 failures through production C++ functions, with OS sleep mocked.

The test extracts the actual wake handlers and retry functions from main.cpp.
It never sleeps this computer or suspends a user process. Run from repo root.
"""
from pathlib import Path
import argparse
import re
import subprocess
import hashlib
import json

parser = argparse.ArgumentParser()
parser.add_argument('--source', type=Path, default=Path('native/main.cpp'))
parser.add_argument('--output', type=Path, default=Path('../../Build/Validation/SleepNonUserWake'))
args = parser.parse_args()
source = args.source.read_text(encoding='utf-8-sig')
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
def mask_cpp(text):
    tokens = re.compile(r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"(?P<rawtag>[^ ()\\\t\r\n]{0,16})\(.*?\)(?P=rawtag)"|"(?:\\.|[^"\\])*"|\x27(?:\\.|[^\x27\\])*\x27', re.S)
    return tokens.sub(lambda match: ''.join('\n' if c == '\n' else ' ' for c in match.group()), text)

masked = mask_cpp(source)

def braced(opening, text=None):
    text = masked if text is None else text
    if opening < 0 or opening >= len(text) or text[opening] != '{':
        raise RuntimeError('native body opening is not a brace')
    end, depth = opening + 1, 1
    while depth and end < len(text):
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    if depth:
        raise RuntimeError('native body is incomplete')
    return end

def function(name):
    matches = list(re.finditer(r'\bstatic\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*\{', masked))
    if len(matches) != 1:
        raise RuntimeError('native function not unique: ' + name)
    match = matches[0]; opening = masked.index('{', match.start())
    return source[match.start():braced(opening)]

def unique_anchor(anchor):
    positions = [match.start() for match in re.finditer(re.escape(anchor), masked)]
    if len(positions) != 1:
        raise RuntimeError('native handler anchor not unique: ' + anchor)
    return positions[0]

def body_after(anchor):
    start = unique_anchor(anchor); opening = masked.index('{', start)
    return source[opening + 1:braced(opening) - 1]

def case_body(start, end):
    # The next requested case may have an unrelated new case inserted before it.
    # Match the complete target braces, never the whole textual interval.
    begin = unique_anchor(start); opening = masked.index('{', begin)
    finish = braced(opening); next_case = unique_anchor(end)
    if finish > next_case:
        raise RuntimeError('native case crosses its requested successor')
    return source[opening + 1:finish - 1]

# The lexical scanner must ignore decoy braces and stop before a neighboring case.
_probe = 'case TARGET: { const char* p=R"x(} case NEXT: {)x"; /* } */ if(true){} } case OTHER: {} case NEXT: {}'
_probe_mask = mask_cpp(_probe)
_probe_open = _probe_mask.index('{')
_probe_end = braced(_probe_open, _probe_mask)
assert 'case OTHER:' not in _probe[:_probe_end] and _probe_mask[_probe_end:].lstrip().startswith('case OTHER:')
try: braced(0, '{')
except RuntimeError: pass
else: raise AssertionError('incomplete native case accepted')


assert 'SG_USER_STANDBY_DEVICE_DELAY_MS = 120000ULL' in source
assert 'SG_NON_USER_WAKE_CLASSIFY_MS = 2000ULL' in source
for broadcast in ['PBT_APMRESUMEAUTOMATIC', 'PBT_APMRESUMESUSPEND']:
    body = body_after('else if (w == ' + broadcast + ')')
    assert 'sgObserveNonUserWake(' in body
    if broadcast == 'PBT_APMRESUMESUSPEND':
        assert body.index('!sgModernStandbyRepairSupported()') < body.index('sgObserveNonUserWake(')
        assert body.index('confirmedUserWake()') < body.index('sgObserveNonUserWake(')
assert 'sgArmNonUserWakeGuard("sleep-trigger")' in function('sgMarkSleepTrigger')
assert 'request(caps,' in function('sgRequestSystemSleep')
assert 'SetSuspendState(mode==Mode::Hibernate ? TRUE : FALSE,FALSE,FALSE)' in function('sgRequestSystemSleep')
assert 'g_sgPauseResume' not in function('sgEvaluateExternalDeviceWake')
assert 'sgCancelEntryFailureRetry("entry-failure-setting-disabled")' in function('sgSaveConfig')

assert 'if (entering) sgRememberKernelSleepEntry(reason, eventFileTime);' in function('sgKernelPowerEventCallback')
assert 'sgPublishSleepEventTime(g_sgOriginalPowerButtonWakeFileTime,eventFileTime);' in function('sgKernelPowerEventCallback')
names = [
    'sgUserWakeCancelsSleepIntent', 'sgPublishSleepEventTime', 'sgRememberKernelSleepEntry',
    'sgSleepRetryActive', 'sgMarkInternalSleepRequest', 'sgClearInternalSleepRequest',
    'sgInternalSleepRequestMatches506', 'sgModernStandbyRepairSupported', 'sgNonUserWakeGuardEnabled',
    'sgSleepTriggerOrigin', 'sgRefreshSleepTriggerOrigin', 'sgExternalDeviceWakeIntentAge', 'sgClearUnexpectedWake', 'sgArmNonUserWakeGuard',
    'sgEvaluateExternalDeviceWake', 'sgObserveNonUserWake',
    'sgNoteExternalDeviceNodeChange', 'sgNoteExternalDeviceAcDcChange',
    'sgNoteExternalDeviceKernel507Reason5', 'sgNoteExternalDeviceKernel507Reason7',
    'sgMarkSleepTrigger', 'sgStopSleepRetry', 'sgRealWake',
    'sgFinishSleepRetryFailure', 'sgScheduleNextSleepRetry', 'sgCancelEntryFailureRetry', 'sgDispatchSameModeRetry',
    'sgStartSleepRetry', 'sgAdvanceSleepRetry', 'sgS0EntryFailureEligible',
    'sgKernelPowerEventFileTime',
]
functions = '\n\n'.join(function(name) for name in names)
handlers = '\n'.join([
    'int handle506(WPARAM w, LPARAM l) {\n' + case_body('case WM_SG_S0_INTENT:', 'case WM_SG_S0_WAKE:') + '\n}',
    'int handle507(WPARAM w, LPARAM l) {\n' + case_body('case WM_SG_S0_WAKE:', 'case WM_SG_S4_WAKE:') + '\n}',
    'int resumeAutomatic() {\n' + body_after('else if (w == PBT_APMRESUMEAUTOMATIC)') + '\nreturn TRUE;\n}',
    'int resumeInteractive() {\n' + body_after('else if (w == PBT_APMRESUMESUSPEND)') + '\nreturn TRUE;\n}',
    'int resumeCritical() {\n' + body_after('else if (w == PBT_APMRESUMECRITICAL)') + '\nreturn TRUE;\n}',
    'int handleS4() {\n' + source[unique_anchor('case WM_SG_S4_WAKE:')+len('case WM_SG_S4_WAKE:'):unique_anchor('case WM_SG_RESUME_GAME_FOCUS:')] + '\n}',
])
suspend = body_after('else if (w == PBT_APMSUSPEND)')
# Execute the production retry-confirmation branch; non-retry setup is covered
# by the production sgMarkSleepTrigger and the single-PID companion selftest.
retry_start = suspend.index('if (sgSleepRetryActive())')
retry_open = suspend.index('{', retry_start)
retry_mask = mask_cpp(suspend)
retry_end = braced(retry_open, retry_mask)
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
#include "sleep_retry_budget.h"
#include "sleep_request_fallback.h"
#include "fan_physical_wake.h"
#include <new>
using json = nlohmann::json;
#define GetTickCount64 fakeGetTickCount64
#define SetTimer fakeSetTimer
#define KillTimer fakeKillTimer
static constexpr UINT_PTR SG_RETRY_TIMER_ID=0xA20C;
static constexpr ULONGLONG SG_USER_STANDBY_DEVICE_DELAY_MS=120000, SG_NON_USER_WAKE_CLASSIFY_MS=2000, SG_PAUSE_READY_WAIT_MAX_MS=2000;
static constexpr UINT SG_PAUSE_READY_POLL_MS=50;
static constexpr ULONGLONG SG_ENTRY_RETRY_DELAYS_MS[]={500,1000,2000};
static constexpr unsigned SG_MAX_ENTRY_RETRIES=3;
ymcc::sleeprepair::Budget g_sgAutomaticSleepBudget{3};
ymcc::fan_wake::Observation g_fanPhysicalWake;
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
struct SgSystemSleepRequestResult {bool accepted=true,shutdownPrivilegeEnabled=true;DWORD shutdownPrivilegeError=0,requestError=0;const char* api="SetSuspendState";ymcc::sleeprequest::Mode target=ymcc::sleeprequest::Mode::Sleep;};
std::atomic<ymcc::sleeprequest::Mode> g_sgAutomaticSleepTarget{ymcc::sleeprequest::Mode::None};
bool sgAutomaticClassicSleepPending(){const auto mode=g_sgAutomaticSleepTarget.load();return mode==ymcc::sleeprequest::Mode::Sleep||mode==ymcc::sleeprequest::Mode::Hibernate;}
HWND g_hwnd=reinterpret_cast<HWND>(1);
ULONGLONG nowTick=10000, nowFile=epoch+10000*10000ULL, generation=2, timerDue=0;
ULONGLONG g_sgIgnoredEarlyWakeFileTime=0,g_sgRetryDueTick=0,g_sgSleepTriggerTick=0,g_sgLastS0WakeTick=0,g_sgLastPowerButtonWakeTick=0,g_sgModernStandbyGeneration=0,g_sgModernWakeClassifiedTick=0,g_lastResumeNotifyTick=0;
double g_sgSleepTriggerEpoch=0;
std::atomic<ULONGLONG> g_sgLastPowerButtonSleepIntentFileTime{0},g_sgLastPowerButtonWakeEventFileTime{0},g_sgPauseWorkCompletedGeneration{0};
std::atomic<int> g_sgLastKernel506Reason{-1};
std::atomic<ULONGLONG> g_sgLatestUserSleepEntryFileTime{0},g_sgClosedSleepIntentFileTime{0},g_sgOriginalPowerButtonWakeFileTime{0};
std::atomic<ULONGLONG> g_sgLastKernel506EventFileTime{0},g_sgLastKernel507EventFileTime{0};
std::atomic<bool> g_sensorSuspendCallbackActive{false};
std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
std::atomic<bool> g_sgSleepCycleActive{false},g_sgGameActuallySuspended{false},g_sgInSuspend{false},g_inputReady{true},g_inputHostPowerAdmissionRevoked{false};
bool g_guardEnabled=true,g_sgResleepEnabled=true,g_sgRetryEntryFailure=true,g_sgRepairEligible=false,g_sgRetryInProgress=false,g_sgNonUserWakeObserved=false,g_sgSleepIntentArmed=false,g_sgModernStandbyActive=false,g_sgModernWakeClassified=false;
bool g_sgSleepQueryGateClosed=false,g_sgSleepQueryOwnsGeneration=false;
unsigned long long g_sgSleepQueryGeneration=0;
int g_sgLastS0WakeReason=-1;
bool requestAccepted=true,keepInput=true,frozen=false,platformAoAc=true,platformCapsKnown=true;
int platformQueries=0;
bool sgPlatformIsAlwaysOnAlwaysConnected(){++platformQueries;return platformCapsKnown&&platformAoAc;}
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
static void sgAbnormalArm(int,ULONGLONG){}
static void sgAbnormalStop(const char*){}
static void sgAbnormalInternalEntry(){}
static void sleepRecoveryAutoArm(unsigned long long){} // automatic owner separately tested; this fixture keeps opt-in off
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
 sgRealWake(w==SgWork::WakeSuspend?"resume_suspend":w==SgWork::WakeHibernate?"resume_hibernate":"resume_auto",generation);
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
 g_sgAutomaticSleepBudget.confirmedUserWake();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::None;
 g_fanPhysicalWake.~Observation();new (&g_fanPhysicalWake) ymcc::fan_wake::Observation();g_fanPhysicalWake.generation(generation);
 g_sgLastPowerButtonSleepIntentFileTime=0;g_sgLastPowerButtonWakeEventFileTime=0;g_sgPauseWorkCompletedGeneration=2;
 g_sgLatestUserSleepEntryFileTime=0;g_sgClosedSleepIntentFileTime=0;g_sgOriginalPowerButtonWakeFileTime=0;
 g_guardEnabled=true;g_sgResleepEnabled=true;g_sgRetryEntryFailure=true;g_sgRepairEligible=false;
 g_sgLastKernel506EventFileTime=0;g_sgLastKernel507EventFileTime=0;g_sensorSuspendCallbackActive=false;
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
void dispatch(){assert(timerDue);at((std::max)(nowTick,timerDue));sgDispatchSameModeRetry();}
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
 if(fixture.value("kind",std::string{})=="archived-delayed-entry"){
  const auto original=std::stoull(fixture["entryFileTime"].get<std::string>());
  const auto normalized=epoch+10000*10000ULL;
  const auto eventTime=[&](const json& value){return normalized+std::stoull(value.get<std::string>())-original;};
  for(const auto& e:fixture["events"]){
   nowFile=eventTime(e["observedFileTime"]);nowTick=(nowFile-epoch)/10000ULL;
   const auto kind=e["kind"].get<std::string>();
   if(kind=="kernel-callback"){
    const int id=e["id"],reason=e["reason"];const auto stamp=eventTime(e["eventFileTime"]);
    if(id==506){sgRememberKernelSleepEntry(reason,stamp);g_sgLastKernel506Reason=reason;g_sgLastKernel506EventFileTime=stamp;}
    else{g_sgLastKernel507EventFileTime=stamp;if(reason==1){g_sgLastPowerButtonWakeEventFileTime=stamp;sgPublishSleepEventTime(g_sgOriginalPowerButtonWakeFileTime,stamp);}}
   }else if(kind=="suspend")sleepIntent();
   else if(kind=="resume-auto")resumeAutomatic();
   else if(kind=="resume-interactive")resumeInteractive();
   else if(kind=="kernel-gui"){
    const int id=e["id"],reason=e["reason"];const auto stamp=eventTime(e["eventFileTime"]);
    if(id==506)handle506(reason,stamp);else handle507(reason==1?1:2,reason);
   }else assert(false&&"unknown archived event kind");
  }
  ULONGLONG originalAgeMs=0;
  assert(sgExternalDeviceWakeIntentAge(originalAgeMs));
  assert(originalAgeMs==(g_sgLastKernel507EventFileTime.load()-normalized)/10000ULL);
  if(timerDue)dispatch();
  assert(requestCount==fixture["expectedWakeCyclesAfterFix"].get<int>());
  assert(resumeCount==0);
  std::cout<<"PASS fixture "<<path<<": original delayed506 age is retained; "<<requestCount<<" guarded wake request (mocked)\n";++passed;return;
 }
 for(const auto& e:fixture["events"]){
  advance(e["atMs"].get<ULONGLONG>());std::string kind=e["kind"];
  if(kind=="broadcast"){
   int code=e["code"];
   if(code==4){if(!confirmRetrySuspend())sleepIntent();}
   else if(code==18)resumeAutomatic();else if(code==7)resumeInteractive();
  }else if(kind=="kernel"){
   int id=e["id"],reason=e["reason"];
   if(id==506){const auto stamp=epoch+e["eventAtMs"].get<ULONGLONG>()*10000;sgRememberKernelSleepEntry(reason,stamp);g_sgLastKernel506Reason=reason;handle506(reason,stamp);}
   else{const auto stamp=epoch+e["eventAtMs"].get<ULONGLONG>()*10000;g_sgLastKernel507EventFileTime=stamp;if(reason==1){g_sgLastPowerButtonWakeEventFileTime=stamp;sgPublishSleepEventTime(g_sgOriginalPowerButtonWakeFileTime,stamp);}handle507(reason==1?1:2,reason);}
  }else if(kind=="device")sgNoteExternalDeviceNodeChange();else sgNoteExternalDeviceAcDcChange();
 }
 advance(nowTick+10000);
 assert(requestCount==fixture["expectedWakeCycles"].get<int>());
 assert(resumeCount==0);
 std::cout<<"PASS fixture "<<path<<": "<<requestCount<<" guarded USB4 wake cycles submit SetSuspendState (mocked)\n";++passed;
}

void verifyActualFanRetryBoundary(){
 reset();sleepIntent();const auto physical=g_fanPhysicalWake.recordSleep(generation,nowFile-20);
 assert(physical&&g_fanPhysicalWake.recordWake(generation,nowFile-10,false,true));assert(g_fanPhysicalWake.physical(physical));
 sgStartSleepRetry(SgRetryKind::EntryFailure,"pure-fan-retry-boundary");
 assert(sgSleepRetryActive()&&timerDue&&requestCount==0&&!g_fanPhysicalWake.physical(physical)&&g_fanPhysicalWake.awaitingWake(physical));
 auto stale=ymcc::fan_wake::Observation();const auto g=stale.recordSleep(1,100);assert(g&&stale.recordWake(1,110,false,true));
 stale.retryBoundary(2,120);assert(stale.physical(g));stale.retryBoundary(1,120);assert(!stale.physical(g)&&stale.awaitingWake(g));
 assert(!stale.recordWake(1,119,false,true)&&!stale.recordWake(1,130,true,true));
 std::cout<<"PASS actual fan pure-state retry boundary clears wake only for the matching shared generation; no hardware terminal\n";
}

int main(int argc,char** argv){
 std::cout<<std::unitbuf;
 if(argc>1&&(strcmp(argv[1],"--s3")==0||strcmp(argv[1],"--caps-unknown")==0)){
  platformAoAc=false;platformCapsKnown=strcmp(argv[1],"--caps-unknown")!=0;
  for(bool pause:{false,true})for(bool keep:{false,true})for(ULONGLONG age:{0ULL,119999ULL,120000ULL,600000ULL}){
   reset(pause);keepInput=keep;sleepIntent();at(10000+age);
   resumeAutomatic();assert(!requestCount&&!timerDue);resumeInteractive();
   assert(!requestCount&&!timerDue&&!sgSleepRetryActive()&&!frozen);
   assert(resumeCount==(pause?1:0));
   for(int i=0;i<100;++i){sgNoteExternalDeviceNodeChange();sgNoteExternalDeviceAcDcChange();
    handle506(1,nowFile);handle507(2,7);resumeAutomatic();resumeInteractive();}
   assert(!requestCount&&!timerDue&&!sgSleepRetryActive()&&!g_sgModernStandbyActive);
  }
  pass("S3 user wake wins before S0 classification, every age/game/input combination and event storm");
  reset(true);sleepIntent();g_sgSleepTriggerTick=nowTick;sgStartSleepRetry(SgRetryKind::EntryFailure,"s3-entry-failure");
  assert(timerDue);resumeInteractive();assert(!timerDue&&!sgSleepRetryActive()&&!requestCount&&!frozen);
  pass("S3 interactive wake cancels pending failure retry and recovers the owned game");
  reset(true);sleepIntent();requestAccepted=false;sgStartSleepRetry(SgRetryKind::EntryFailure,"s3-entry-failure");
  dispatch();dispatch();dispatch();assert(requestCount==3&&!timerDue&&!frozen);
  for(int i=0;i<100;++i){++generation;sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"repeat-failure");
   if(timerDue)dispatch();}
  assert(requestCount==3&&!timerDue);
  pass("S3 request rejection storm is bounded across generation and task resets");
  resumeInteractive();sleepIntent();requestAccepted=true;sgStartSleepRetry(SgRetryKind::EntryFailure,"new-user-sleep");
  dispatch();assert(requestCount==4);assert(platformQueries==1);
  pass("confirmed S3 user wake allows a fresh bounded repair; capabilities queried only once");
  std::cout<<"S3/capability fail-closed native selftest: "<<passed<<" groups PASS (actual production handlers; OS/PID boundaries mocked)\n";
  verifyActualFanRetryBoundary();
  return 0;
 }
 // Accepted requests used to reset the counter at each confirmed suspend.
 reset(true);sleepIntent();at(140000);
 for(int i=0;i<200;++i){resumeAutomatic();resumeInteractive();handle507(2,5);
  if(timerDue)dispatch();if(sgSleepRetryActive())confirmRetrySuspend();
  assert(requestCount<=3);at(nowTick+130000);}
 assert(requestCount==3&&!timerDue&&!frozen&&resumeCount==1);
 pass("200 accepted sleep/wake cycles stop after 3 automatic requests and recover the game");
 for(int i=0;i<100;++i){++generation;sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"late-failure");
  if(timerDue)dispatch();handle506(3,nowFile);handle507(2,7);sgAdvanceSleepRetry("duplicate-failure");
  assert(requestCount==3&&!timerDue);}
 pass("generation resets, late 506, entry failures and duplicate notifications cannot refill exhausted budget");
 powerButton();at(nowTick+10);sleepIntent();at(nowTick+130000);resumeAutomatic();dispatch();assert(requestCount==4);
 pass("confirmed S0 power-button wake permits a new bounded chain");
 reset();sleepIntent();
 for(int i=0;i<3;++i){sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-failure-cycle");dispatch();assert(confirmRetrySuspend());}
 assert(g_sgTask.retryAttempt==0&&g_sgAutomaticSleepBudget.requests()==3);
 sgStartSleepRetry(SgRetryKind::EntryFailure,"fourth-entry-failure");assert(requestCount==3&&!timerDue);
 pass("successful EntryFailure sleeps do not reset the chain budget");
 reset();sleepIntent();requestAccepted=false;sgStartSleepRetry(SgRetryKind::EntryFailure,"mixed-entry");dispatch();
 requestAccepted=true;dispatch();assert(confirmRetrySuspend());at(140000);resumeAutomatic();dispatch();assert(confirmRetrySuspend());
 at(270000);resumeAutomatic();assert(requestCount==3&&!timerDue);
 pass("entry failure and unexpected wake share one cumulative request budget");
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
 for(auto target:{ymcc::sleeprequest::Mode::Sleep,ymcc::sleeprequest::Mode::Hibernate}){
  reset(true);sleepIntent();g_sgAutomaticSleepTarget=target;at(140000);resumeAutomatic();
  assert(!timerDue&&!requestCount&&resumeCount==0&&frozen);handle507(2,5);assert(!timerDue&&!requestCount);
  resumeInteractive();assert(resumeCount==1&&!frozen&&!sgSleepRetryActive()&&g_sgAutomaticSleepTarget==ymcc::sleeprequest::Mode::None);
 }
 pass("ordinary/hibernate fallback automatic wake stays held; final interactive wake cancels and recovers exact lease");
 reset(true);sleepIntent();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::Hibernate;handleS4();
 assert(resumeCount==1&&!frozen&&g_sgAutomaticSleepTarget==ymcc::sleeprequest::Mode::None&&!g_sgModernStandbyActive);
 pass("actual S4 kernel wake finalizes fallback and recovers exact original lease");
 reset(true);sleepIntent();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::Hibernate;resumeCritical();
 assert(resumeCount==1&&!frozen&&g_sgAutomaticSleepTarget==ymcc::sleeprequest::Mode::None&&!g_sgModernStandbyActive);
 pass("critical-only S4 fallback resume cannot strand the lease");
 reset();handle506(4,nowFile);assert(!g_sgRepairEligible&&!g_sgModernStandbyActive&&!beginCount);
 pass("ordinary non-user506does not manufacture a new user sleep intent");
 reset();sleepIntent();g_sgTask.retryKind=SgRetryKind::EntryFailure;g_sgRetryInProgress=true;
 sgMarkInternalSleepRequest(SgRetryKind::EntryFailure);g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::ModernStandby;
 assert(sgInternalSleepRequestMatches506(4,nowFile));handle506(4,nowFile);assert(beginCount==0&&g_sgModernStandbyActive);
 g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::Sleep;assert(!sgInternalSleepRequestMatches506(4,nowFile));
 pass("monitor-off506can only join the timestamp-correlated original modern retry");
 ULONGLONG second=sgKernelPowerEventFileTime("<TimeCreated SystemTime='2026-09-30T15:14:50Z'/>");
 assert(sgKernelPowerEventFileTime("<TimeCreated SystemTime='2026-09-30T15:14:50.1234567Z'/>")-second==1234567);
 assert(sgKernelPowerEventFileTime("<TimeCreated SystemTime=\"2026-09-30T15:14:50.5Z\"/>")-second==5000000);
 pass("power event parser preserves sub-second button/sleep ordering");
 reset(true);sleepIntent();g_powerLifecycle=PowerLifecycle::Suspending;sgStartSleepRetry(SgRetryKind::EntryFailure,"off-pending");
 g_sgRetryEntryFailure=false;sgCancelEntryFailureRetry("off");g_sgRetryEntryFailure=true;at(nowTick+1000);sgDispatchSameModeRetry();
 assert(!requestCount&&!timerDue&&!sgSleepRetryActive()&&!frozen);
 pass("OFF retires old EntryFailure work; ON cannot revive it; failed owned game is recovered");
 reset(true);sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"off-during-standby");g_sgRetryEntryFailure=false;
 sgCancelEntryFailureRetry("off");assert(!requestCount&&!timerDue&&!sgSleepRetryActive()&&frozen&&resumeCount==0);
 powerButton();assert(!frozen&&resumeCount==1);
 pass("OFF during Windows standby preserves lease until confirmed button wake");
 reset(true);sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"wake-before-timer");at(timerDue-1);
 g_sgLastPowerButtonWakeEventFileTime=nowFile;dispatch();assert(!requestCount&&!sgSleepRetryActive()&&!frozen&&g_sgAutomaticSleepBudget.requests()==0);
 pass("callback-side current button wake before EntryFailure dispatch cancels without spending budget");
 reset();sleepIntent();g_sgLastPowerButtonWakeEventFileTime=g_sgLastPowerButtonSleepIntentFileTime.load()-1;
 sgStartSleepRetry(SgRetryKind::EntryFailure,"new-manual");dispatch();assert(requestCount==1);
 pass("previous-cycle button wake never blocks a new eligible manual sleep");
 reset(true);sleepIntent();g_powerLifecycle=PowerLifecycle::Suspending;sgStartSleepRetry(SgRetryKind::EntryFailure,"accepted");
 dispatch();g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::ModernStandby;g_sgRetryEntryFailure=false;sgCancelEntryFailureRetry("off");
 assert(requestCount==1&&!sgSleepRetryActive()&&frozen&&g_sgInternalSleepRequest.kind==SgRetryKind::EntryFailure);
 pass("OFF cannot undo accepted OS call; late Windows correlation remains owned");

 reset(true);g_sgResleepEnabled=false;sleepIntent();const auto separateIntent=g_sgLastPowerButtonSleepIntentFileTime.load();
 assert(separateIntent==nowFile&&g_sgRepairEligible);
 sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-button");at(timerDue-1);
 g_sgLastPowerButtonWakeEventFileTime=nowFile;dispatch();
 assert(!requestCount&&!sgSleepRetryActive()&&!frozen&&resumeCount==1&&g_sgAutomaticSleepBudget.requests()==0);
 pass("EntryFailure records intent with non-user option OFF and no506; current callback wake vetoes before budget");
 reset();g_sgResleepEnabled=false;sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-valid");dispatch();
 assert(requestCount==1&&sgSleepRetryActive());
 pass("non-user option OFF still permits an eligible EntryFailure without a user wake");
 reset();g_sgResleepEnabled=false;sleepIntent();g_sgLastPowerButtonWakeEventFileTime=g_sgLastPowerButtonSleepIntentFileTime.load()-1;
 sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-old-wake");dispatch();assert(requestCount==1);
 pass("non-user option OFF never lets an old wake veto fresh manual sleep");
 reset();g_sgResleepEnabled=false;sleepIntent();g_sgLastPowerButtonWakeEventFileTime=g_sgLastPowerButtonSleepIntentFileTime.load();
 sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-equal-wake");dispatch();
 assert(!requestCount&&!sgSleepRetryActive()&&g_sgAutomaticSleepBudget.requests()==0);
 pass("same-timestamp current wake veto remains effective with non-user option OFF");
 reset(true);g_sgResleepEnabled=false;sleepIntent();sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-processed");
 at(timerDue-1);powerButton();at(nowTick+10);sleepIntent();const auto freshIntent=g_sgLastPowerButtonSleepIntentFileTime.load();
 assert(freshIntent==nowFile&&freshIntent>separateIntent);sgStartSleepRetry(SgRetryKind::EntryFailure,"entry-only-fresh");dispatch();assert(requestCount==1);
 pass("processed manual wake then a fresh sleep obtains a new independent anchor");
 reset();g_sgResleepEnabled=false;g_sgTask.mode=SgSleepMode::S3;sgMarkSleepTrigger();
 assert(!g_sgRepairEligible&&!g_sgLastPowerButtonSleepIntentFileTime.load());
 reset();g_sgResleepEnabled=false;g_sgTask.mode=SgSleepMode::S4;g_sgSleepIntentArmed=true;sgMarkSleepTrigger();
 assert(!g_sgLastPowerButtonSleepIntentFileTime.load());
 pass("independent anchor never manufactures an unarmed or hibernate sleep intent");
 reset();g_sgResleepEnabled=false;const auto kernelIntent=nowFile-1000;g_sgLastPowerButtonSleepIntentFileTime=kernelIntent;sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==kernelIntent);
 pass("existing kernel origin remains exact when the non-user option is OFF");
 reset();g_sgResleepEnabled=false;sleepIntent();const auto originalIntent=g_sgLastPowerButtonSleepIntentFileTime.load();at(nowTick+100);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==originalIntent);
 pass("duplicate sleep trigger cannot refresh the original intent age");
 reset();g_sgResleepEnabled=false;sleepIntent();at(140000);resumeAutomatic();sgNoteExternalDeviceNodeChange();sgNoteExternalDeviceAcDcChange();
 assert(!requestCount&&!timerDue&&!sgSleepRetryActive());
 pass("recording an independent anchor never enables the disabled power-change guard");


 reset();sgRememberKernelSleepEntry(28,nowFile);sgRememberKernelSleepEntry(1,0);sgRememberKernelSleepEntry(3,nowFile+1);
 assert(!g_sgLatestUserSleepEntryFileTime.load());
 pass("non-user reasons, missing original time and future events never create a callback origin");
 reset();const auto callbackFirst=nowFile;sgRememberKernelSleepEntry(1,callbackFirst);at(nowTick+1);const auto callbackSecond=nowFile;
 sgRememberKernelSleepEntry(3,callbackSecond);sgRememberKernelSleepEntry(1,callbackFirst);sgRememberKernelSleepEntry(28,nowFile);
 assert(g_sgLatestUserSleepEntryFileTime.load()==callbackSecond);
 sgNoteExternalDeviceNodeChange();sgNoteExternalDeviceAcDcChange();assert(!g_sgLastPowerButtonSleepIntentFileTime.load()&&!requestCount&&!timerDue);
 pass("callback watermark is monotonic event data only; awake USB activity cannot arm sleep");
 reset();const auto lateOriginal=nowFile;sgRememberKernelSleepEntry(1,lateOriginal);at(nowTick+658615);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==lateOriginal);
 at(nowTick+182);resumeAutomatic();dispatch();assert(requestCount==1);
 pass("delayed suspend GUI handling inherits original506 age instead of182ms");
 reset();const auto closedEntry=nowFile;sgRememberKernelSleepEntry(1,closedEntry);at(nowTick+1);sgClearUnexpectedWake("closed-cycle",true);
 at(nowTick+1000);sleepIntent();assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile&&g_sgLastPowerButtonSleepIntentFileTime.load()!=closedEntry);
 pass("closed cycle watermark prevents reusing an old pending506 for a new sleep");
 reset();const auto beforeButton=nowFile;sgRememberKernelSleepEntry(1,beforeButton);at(nowTick+1);g_sgLastPowerButtonWakeEventFileTime=nowFile;
 at(nowTick+1000);sleepIntent();assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);
 sgStartSleepRetry(SgRetryKind::EntryFailure,"fresh-after-callback-button");dispatch();assert(requestCount==1);
 pass("old callback entry before confirmed button wake cannot replace a fresh manual origin");
 reset();sgRememberKernelSleepEntry(1,nowFile);g_sgIgnoredEarlyWakeFileTime=nowFile+1;at(nowTick+1000);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);
 pass("an already ignored early-wake cycle cannot be inherited by the next trigger");
 reset();sgRememberKernelSleepEntry(1,nowFile);sgMarkInternalSleepRequest(SgRetryKind::EntryFailure);at(nowTick+1);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);
 pass("timestamp-correlated internal506 is never inherited as a new user intent");
 reset();sgRememberKernelSleepEntry(1,nowFile);g_sgAutomaticSleepTarget=ymcc::sleeprequest::Mode::ModernStandby;at(nowTick+1);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);
 pass("accepted automatic sleep target cannot supply an inherited user origin");
 reset();const auto exactOrigin=nowFile-10000;g_sgLastPowerButtonSleepIntentFileTime=exactOrigin;sgRememberKernelSleepEntry(1,nowFile);
 at(nowTick+100);sleepIntent();assert(g_sgLastPowerButtonSleepIntentFileTime.load()==exactOrigin);
 pass("already admitted original event time is not replaced by a callback candidate");
 reset();sgRememberKernelSleepEntry(1,nowFile);g_sgAutomaticSleepBudget.consume();g_sgAutomaticSleepBudget.consume();
 sgClearUnexpectedWake("no-owned-origin",true);assert(g_sgClosedSleepIntentFileTime.load()==nowFile&&g_sgAutomaticSleepBudget.requests()==2);
 pass("retiring pending data without a current origin neither arms sleep nor refills budget");
 reset();sgClearUnexpectedWake("past-cycle",true);at(nowTick+10);const auto nextEntry=nowFile;sgRememberKernelSleepEntry(1,nextEntry);
 at(nowTick+10);g_sgLastKernel507EventFileTime=nowFile;at(nowTick+200000);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nextEntry);
 pass("a new unretired entry remains usable when its507callback also precedes the GUI suspend");
 reset(true);sgRememberKernelSleepEntry(1,nowFile);at(nowTick+130000);sleepIntent();resumeAutomatic();
 g_sgLastPowerButtonWakeEventFileTime=nowFile;handle507(1,1);if(timerDue)dispatch();
 assert(!requestCount&&!sgSleepRetryActive()&&!frozen);
 pass("confirmed manual button wake still vetoes a retry armed from inherited event time");


 reset();const auto callbackAfterGui=nowFile;at(nowTick+658615);sleepIntent();
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==nowFile);sgRememberKernelSleepEntry(1,callbackAfterGui);
 at(nowTick+182);resumeAutomatic();dispatch();assert(requestCount==1);
 pass("a506callback after suspend GUI handling is reconciled before the120-second wake decision");

 reset();sleepIntent();const auto frozenTick=nowTick;const auto frozenOrigin=nowFile;
 nowFile+=658419ULL*10000;g_sgLastKernel507EventFileTime=nowFile;ULONGLONG absoluteAgeMs=0;
 assert(sgExternalDeviceWakeIntentAge(absoluteAgeMs)&&absoluteAgeMs==658419&&nowTick==frozenTick&&!timerDue&&!requestCount);
 assert(g_sgLastPowerButtonSleepIntentFileTime.load()==frozenOrigin);
 pass("sleep duration follows absolute event times without any timer callback during sleep");
 reset();sleepIntent();const auto shortOrigin=nowFile;at(nowTick+200000);
 g_sgLastKernel507EventFileTime=shortOrigin+99000ULL*10000;ULONGLONG shortAgeMs=0;
 assert(!sgExternalDeviceWakeIntentAge(shortAgeMs)&&shortAgeMs==99000);resumeAutomatic();
 assert(!requestCount&&!timerDue&&!g_sgLastPowerButtonSleepIntentFileTime.load());
 pass("a99-second sleep stays short when its wake callback arrives after200seconds");
 reset();sleepIntent();const auto lateWakeOrigin=nowFile;at(nowTick+140000);resumeAutomatic();assert(timerDue);
 g_sgLastKernel507EventFileTime=lateWakeOrigin+99000ULL*10000;dispatch();
 assert(!requestCount&&!sgSleepRetryActive()&&!g_sgAutomaticSleepBudget.requests());
 pass("late original507time cancels a tentative retry before any request or budget charge");
 reset();sleepIntent();g_sgLastKernel507EventFileTime=nowFile-1;at(nowTick+130000);ULONGLONG oldWakeAgeMs=0;
 assert(sgExternalDeviceWakeIntentAge(oldWakeAgeMs)&&oldWakeAgeMs==130000);
 pass("a507from before the current sleep intent cannot shorten the current standby age");
 reset();sleepIntent();at(nowTick+130000);g_sgLastKernel507EventFileTime=nowFile+1;ULONGLONG futureWakeAgeMs=0;
 assert(sgExternalDeviceWakeIntentAge(futureWakeAgeMs)&&futureWakeAgeMs==130000);
 pass("a future507timestamp is not accepted as this cycle's wake endpoint");
 for(const auto elapsed:{119999ULL,120000ULL}){
  reset();sleepIntent();const auto origin=nowFile;at(nowTick+300000);
  g_sgLastKernel507EventFileTime=origin+elapsed*10000;ULONGLONG boundaryAgeMs=0;
  assert(sgExternalDeviceWakeIntentAge(boundaryAgeMs)==(elapsed==120000)&&boundaryAgeMs==elapsed);
 }
 pass("the120-second policy boundary uses original wake time even with delayed collection");

 reset();sleepIntent();at(nowTick+130000);resumeAutomatic();assert(timerDue);
 at(timerDue+2000);const auto delayedDispatchTick=nowTick;const auto delayedDispatchFileTime=nowFile;dispatch();
 assert(requestCount==1&&nowTick==delayedDispatchTick&&nowFile==delayedDispatchFileTime);
 pass("a delayed timer message never rewinds the current time to its scheduled deadline");

 for(const bool nonUserEnabled:{false,true}){
  reset(true);g_sgResleepEnabled=nonUserEnabled;sleepIntent();
  at(nowTick+130000);sgStartSleepRetry(SgRetryKind::EntryFailure,"accepted-before-late-button-callback");dispatch();
  const auto acceptedEntry=nowFile+10000;const auto actualButton=nowFile-10000;
  at(nowTick+2);g_sgLastPowerButtonWakeEventFileTime=actualButton;handle507(1,1);
  const auto afterWakeGeneration=generation;
  handle506(3,acceptedEntry);
  assert(generation==afterWakeGeneration&&!g_sgLastPowerButtonSleepIntentFileTime.load()&&!sgSleepRetryActive()&&!frozen);
  assert(g_sgLastPowerButtonWakeEventFileTime.load()==actualButton);
  sgNoteExternalDeviceNodeChange();resumeAutomatic();if(timerDue)dispatch();
  assert(requestCount==1&&!sgSleepRetryActive());
 }
 pass("late internal506after a completed manual wake cannot resurrect either option's retired cycle");
 for(const bool nonUserEnabled:{false,true}){
  reset();g_sgResleepEnabled=nonUserEnabled;g_sgLastPowerButtonWakeEventFileTime=nowFile-10000;
  const auto humanWatermark=g_sgLastPowerButtonWakeEventFileTime.load();sleepIntent();
  assert(g_sgLastPowerButtonWakeEventFileTime.load()==humanWatermark);
  at(nowTick+130000);sgStartSleepRetry(SgRetryKind::EntryFailure,"fresh-sleep-after-old-wake");dispatch();assert(requestCount==1);
 }
 pass("fresh sleep preserves the human wake watermark and compares it to the new intent instead of erasing it");

 for(const bool nonUserEnabled:{false,true}){
  reset(true);g_sgResleepEnabled=nonUserEnabled;const auto oldWake=nowFile;
  g_sgLastPowerButtonWakeEventFileTime=oldWake;at(nowTick+10);sleepIntent();
  sgStartSleepRetry(SgRetryKind::EntryFailure,"new-sleep-before-old-wake-message");handle507(1,1);
  assert(sgSleepRetryActive()&&frozen&&g_sgLastPowerButtonSleepIntentFileTime.load()>oldWake);
  dispatch();assert(requestCount==1);at(nowTick+10);g_sgLastPowerButtonWakeEventFileTime=nowFile;handle507(1,1);
  assert(!sgSleepRetryActive()&&!frozen&&!g_sgLastPowerButtonSleepIntentFileTime.load());
 }
 pass("an older queued manual wake cannot cancel a newer manual sleep; its own current wake still cancels");

 for(const bool nonUserEnabled:{false,true}){
  reset(true);g_sgResleepEnabled=nonUserEnabled;const auto originalEntry=nowFile;
  sgRememberKernelSleepEntry(1,originalEntry);at(nowTick+130000);
  g_sgLastPowerButtonWakeEventFileTime=nowFile;g_sgLastKernel507EventFileTime=nowFile;
  sgPublishSleepEventTime(g_sgOriginalPowerButtonWakeFileTime,nowFile);
  // An older later-delivered non-user507 cannot erase the original button witness.
  g_sgLastKernel507EventFileTime=originalEntry+10000;
  at(nowTick+2000);sleepIntent();resumeAutomatic();handle507(1,1);
  assert(!sgSleepRetryActive()&&!frozen&&!g_sgLastPowerButtonSleepIntentFileTime.load());
  at(nowTick+130000);sgNoteExternalDeviceNodeChange();if(timerDue)dispatch();assert(!requestCount);
 }
 pass("a delayed suspend GUI after both entry and manual wake callbacks cannot recast the wake as older than the current intent");

 for(int i=1;i<argc;++i)replay(argv[i]);
 std::cout<<"non-user wake native selftest: "<<passed<<" groups PASS (120-second gate; actual production functions; OS/PID boundaries mocked)\n";
 verifyActualFanRetryBoundary();
}
'''

# Bind exact actual function/handler text before compiling; no neighboring fan case.
assert 'WM_FAN_KERNEL_POWER_FACT' not in handlers and 'fanHostQueueSuspend' not in handlers
(out/'actual-production-source-bindings.json').write_text(json.dumps({
 'schemaVersion':1,'source':str(args.source.resolve()),'sourceSha256':hashlib.sha256(args.source.read_bytes()).hexdigest(),
 'functions':[{'name':name,'sha256':hashlib.sha256(function(name).encode()).hexdigest(),'text':function(name)} for name in names],
 'handlersSha256':hashlib.sha256(handlers.encode()).hexdigest(),'handlers':handlers,
 'functionBodiesUniqueAndComplete':True,'neighborHardwareCaseExcluded':True,
 'systemSleepFunctionIsSourceOnly':True,'actualSystemSleepTerminal':'original counted in-memory sgRequestSystemSleep',
 'pureFanBoundaryHeader':'native/fan_physical_wake.h','actual120SecondGateUnchanged':True,
 'originalReplayFixturesUnchanged':True,'realOsSleep':False
},indent=2)+'\n',encoding='utf-8')

cpp = out / 'sleep_non_user_wake_native_selftest.cpp'
cpp.write_text(preamble + '\n' + functions + '\n' + handlers + '\n' + checks, encoding='utf-8')
repo = Path.cwd()
vs = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
assert vs.is_file(), 'MSVC environment not found'
exe = out / 'sleep_non_user_wake_native_selftest.exe'
fixtures = [repo / 'tools/fixtures/sleep_non_user_wake' / name for name in
            ['amd_usb4_failure.json', 'intel_usb4_failure.json', 'amd_power_button_wake.json', 'g3e_usb4_delayed_entry.json']]
cmd = out / 'run-native-selftest.cmd'
toolchain_js='import {calibrationToolchainPreamble} from "./tools/fixtures/hc_reviewed_calibration/toolchain.mjs";process.stdout.write(calibrationToolchainPreamble(process.argv[1]));'
toolchain=subprocess.run(['node','--input-type=module','-e',toolchain_js,str(vs)],cwd=repo,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',check=True).stdout
cmd.write_text(toolchain + f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 /D_CRT_SECURE_NO_WARNINGS "{cpp}" /I"{repo / "deps/json"}" /I"{repo / "native"}" '
               f'/Fo"{out / "sleep_non_user_wake_native_selftest.obj"}" /Fe"{exe}"\r\n'
               f'if not "%errorlevel%"=="0" exit /b %errorlevel%\r\n"{exe}" --s3\r\nif not "%errorlevel%"=="0" exit /b %errorlevel%\r\n'
               f'"{exe}" --caps-unknown\r\nif not "%errorlevel%"=="0" exit /b %errorlevel%\r\n'
               f'"{exe}" ' + ' '.join(f'"{p}"' for p in fixtures) + '\r\n', encoding='ascii')
result = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(out / 'native-selftest.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8', errors='replace'))
raise SystemExit(result.returncode)
