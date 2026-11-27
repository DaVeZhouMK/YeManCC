"""Compile production resume-focus gates/worker/scheduler with deterministic boundaries.
The Win32 companion uses only owned fixture processes/windows; no real game or
sleep event is manipulated. Outputs must stay in Build/Validation.
"""
from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=Path('../../Build/Validation/SleepGameIsolation-20260930'))
parser.add_argument('--sleep-focus-only', action='store_true', help='Exclude the separate WebView summon lane')
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
repo = Path.cwd()
source = (repo/'native/main.cpp').read_text('utf-8-sig')
masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                lambda m: ' '*len(m.group()), source, flags=re.S)

def function(name):
    match = re.search(r'\bstatic\s+[^;{}]*?\b'+re.escape(name)+r'\s*\([^;{}]*?\)\s*(?:noexcept\s*)?\{', masked)
    assert match, f'missing function {name}'
    opening = masked.index('{', match.start())
    depth, end = 1, opening+1
    while depth:
        depth += (masked[end]=='{')-(masked[end]=='}')
        end += 1
    return source[match.start():end]

assert 'AttachThreadInput(' not in masked, 'no YMCC input queue may attach to a foreign game'
assert 'focusSingleResumedGame(rr.pids, target.processCreated, focusOrigin)' in source
worker = function('focusResumeGameWorker')
assert 'g_sgOpMtx' not in worker and 'NtResume' not in worker and 'SendInput' not in worker
assert worker.index('resumeWindowResponds') < worker.index('finalGate') < worker.index('requestWindowForegroundAsync')
ui = function('focusResumeFocusTimerTickOnUiThread')
assert 'poolSubmit' in ui and all(x not in ui for x in ['WaitForSingleObject','Sleep(','SendMessage','resumeWindowResponds'])
nudge = function('wakeFocusNudgeRunOnUiThread')
assert 'focusRememberedGameTarget()' not in nudge and 'target.processCreated' in nudge
assert 'const FocusTargetSnapshot target = g_wakeFocusNudgeTarget;' in nudge
assert 'g_wakeFocusNudgeTarget = GetTickCount64() <= g_rememberedGameDeadline ?' in source

preamble = r"""
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cassert>
#include <atomic>
#include <deque>
#include <functional>
#include <iostream>
#include <mutex>
#include <string>
#include <system_error>
#include <vector>
#include <algorithm>
#include <json.hpp>
using json = nlohmann::json;
static ULONGLONG now=10000, created=123;
static DWORD windowPid=42;
static bool exited=false, openFails=false, responds=true, manualMarker=false, sleepMarker=false;
static bool inWorker=false, queueAccepts=true, postsSucceed=true, queueThrows=false, timersSucceed=true, logThrows=false;
static int probes=0, activations=0, timers=0, closes=0, posts=0;
static std::function<void()> onFindWindow, onProbe, onPos;
static std::deque<std::function<void()>> jobs;
static std::vector<json> logs;
static HWND gameWindow=(HWND)2;
static ULONGLONG fakeTick(){return now;}
static HANDLE fakeOpen(DWORD,BOOL,DWORD pid){assert(inWorker);return (!openFails&&pid==42)?(HANDLE)1:nullptr;}
static DWORD fakeWait(HANDLE,DWORD timeout){assert(timeout==0);return exited?WAIT_OBJECT_0:WAIT_TIMEOUT;}
static BOOL fakeClose(HANDLE){++closes;return TRUE;}
static DWORD fakeWindowThread(HWND hwnd,DWORD* pid){if(pid)*pid=windowPid;return hwnd?9876:0;}
static LRESULT fakeProbe(HWND,UINT msg,WPARAM,LPARAM,UINT flags,UINT timeout,PDWORD_PTR reply){
 assert(inWorker && msg==WM_NULL && timeout==150);
 assert((flags&SMTO_ABORTIFHUNG)&&(flags&SMTO_BLOCK)&&(flags&SMTO_ERRORONEXIT));
 ++probes;if(onProbe)onProbe();*reply=0;return responds?1:0;
}
static BOOL fakeVisible(HWND){return TRUE;}
static BOOL fakeIconic(HWND){return FALSE;}
static BOOL fakeShow(HWND,int){assert(inWorker);return TRUE;}
static BOOL fakePos(HWND,HWND,int,int,int,int,UINT flags){assert(inWorker&&(flags&SWP_ASYNCWINDOWPOS));if(onPos)onPos();return TRUE;}
static BOOL fakeForeground(HWND h){assert(inWorker||h==(HWND)3);++activations;return TRUE;}
static BOOL fakeIsWindow(HWND h){return h!=nullptr;}
static BOOL fakeBring(HWND h){assert(h==(HWND)3);return TRUE;}
static HWND fakeActive(HWND h){assert(h==(HWND)3);return nullptr;}
static HWND fakeFocus(HWND h){assert(h==(HWND)3);return nullptr;}
static UINT_PTR fakeTimer(HWND,UINT_PTR id,UINT,TIMERPROC){++timers;return timersSucceed?id:0;}
static BOOL fakeKill(HWND,UINT_PTR){return TRUE;}
static BOOL fakePost(HWND,UINT,WPARAM w,LPARAM l){assert(w==0&&l==0);++posts;return postsSucceed;}
#define GetTickCount64 fakeTick
#define OpenProcess fakeOpen
#define WaitForSingleObject fakeWait
#define CloseHandle fakeClose
#define GetWindowThreadProcessId fakeWindowThread
#define SendMessageTimeoutW fakeProbe
#define IsWindowVisible fakeVisible
#define IsIconic fakeIconic
#define ShowWindowAsync fakeShow
#define SetWindowPos fakePos
#define SetForegroundWindow fakeForeground
#define IsWindow fakeIsWindow
#define BringWindowToTop fakeBring
#define SetActiveWindow fakeActive
#define SetFocus fakeFocus
#define SetTimer fakeTimer
#define KillTimer fakeKill
#define PostMessageW fakePost
#include "resume_focus.h"
#include "sleep_power_observation.h"
static HWND g_hwnd=(HWND)3;
static bool g_webviewReady=true, selfForeground=true;
static constexpr int COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC=1;
struct FakeControl { void MoveFocus(int){} };
static FakeControl* g_ctrl=nullptr;
static bool focusMainWindowIsForeground(){return selfForeground;}
static std::atomic<bool> g_exitRequested{false},g_inputReady{true};
static std::atomic<DWORD> g_manualPausedPid{0};
enum class PowerLifecycle{Ready,Suspending,Suspended,Resuming};
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static unsigned long long generation=1;
static unsigned long long notification=0, suspendSerial=0, publication=1;
static bool notificationPending=false, publicPowerKnown=true;
static std::uint64_t sleepBoundary=0;
static struct {
 bool pendingSuspend() const {return notificationPending;}
 std::uint64_t observedSleepBoundarySequence() const {return sleepBoundary;}
} g_sleepPowerDispatch;
static int __cdecl readSharedSystemPower(ymcc::sleeppower::SharedPowerV1* out,uint32_t bytes) noexcept {
 using namespace ymcc::sleeppower;
 if(!publicPowerKnown||!out||bytes!=sizeof(*out))return 0;
 *out={};out->generation=generation;out->phase=static_cast<uint32_t>(g_powerLifecycle.load());
 out->observedNotificationSequence=notification;out->suspendSerial=suspendSerial;out->publicationRevision=publication;
 out->flags=kKnown|(out->phase==0?kHardwareAllowed:0)|(notificationPending?kNotificationPending:0);
 return 1;
}
static ymcc::sleeppower::SharedPowerV1 powerOrigin() {
 ymcc::sleeppower::SharedPowerV1 value;
 assert(ymcc::sleeppower::capturePowerObservation(&readSharedSystemPower,value));return value;
}
static bool sharedPowerLifecycleCurrent(const ymcc::sleeppower::SharedPowerV1& value) {
 return ymcc::sleeppower::powerCurrent(&readSharedSystemPower,value);
}
struct FocusTargetSnapshot { bool valid=false; DWORD pid=0; ULONGLONG processCreated=0; };
static FocusTargetSnapshot g_wakeFocusNudgeTarget;
static ymcc::sleeppower::SharedPowerV1 g_wakeFocusNudgePower;
static constexpr UINT kWakeFocusNudgeDelayMs=5000;
static const std::wstring SG_SLEEP_LEASE_DIR=L"sleep",SG_MANUAL_DIR=L"manual";
namespace fspath {
 static bool exists(const std::wstring& p){return p.find(L"manual")==0?manualMarker:sleepMarker;}
 static bool exists(const std::wstring& p,std::error_code& ec){ec.clear();return exists(p);}
}
static ULONGLONG nativeProcessCreatedFromHandle(HANDLE){return created;}
static HWND focusFindWindowForPid(DWORD){if(onFindWindow)onFindWindow();return gameWindow;}
static bool focusQueryProcessIdentity(DWORD,std::wstring*,ULONGLONG* value){*value=created;return !exited;}
static void appendNativeLifecycleLog(const char* event,json value){if(logThrows)throw std::runtime_error("fixture log failure");value["event"]=event;logs.push_back(value);}
template<class...T>static void traceLog(const char*,T...){}
#define RESUME_GAME_FOCUS_TIMER_ID 0xA211
#define WM_SG_RESUME_GAME_FOCUS (WM_USER+18)
static bool poolSubmit(std::function<void()> job){if(queueThrows)throw std::runtime_error("fixture queue allocation");if(!queueAccepts)return false;jobs.push_back(std::move(job));return true;}
"""
begin=source.index('static std::mutex g_resumeFocusMailboxMx;')
end=source.index('static bool captureResumeFocusPower(',begin)
globals=source[begin:end]
names=['focusPidIsManuallyPaused','focusGameControlBlocked','captureResumeFocusPower','focusResumeGate','focusResumeLog',
       'focusResumeTargetPaused','focusResumePost','focusResumeBeforeProbe','focusResumeGameWorker',
       'focusResumeRetryOnUiThread','focusResumeFocusTimerTickOnUiThread',
       'focusResumedGameOnUiThread','focusResumeContinueOnWindowMessage','focusQueueResumedGame','focusSingleResumedGame','wakeFocusNudgeRunOnUiThread']
constants='\n'.join(next(line for line in source.splitlines() if line.startswith(prefix)) for prefix in (
    'static constexpr UINT WM_FAN_KERNEL_POWER_FACT ', 'static constexpr UINT kSharedPowerStateMessage='))
wndproc=function('WndProc')
message_route=re.search(r'if\(g_resumeFocusPostDeferred\.load\(std::memory_order_acquire\)\|\|g_resumeFocusRetryOnMessageAt\)\s*focusResumeContinueOnWindowMessage\(m\);',wndproc)
assert message_route, 'ordinary WndProc must route deferred focus delivery'
checks=r"""
static int passed=0;
static void pass(const char* s){++passed;std::cout<<"PASS "<<s<<"\n";}
static void reset(){
 now=10000;created=123;windowPid=42;exited=openFails=manualMarker=sleepMarker=inWorker=false;
 responds=queueAccepts=postsSucceed=timersSucceed=true;queueThrows=logThrows=false;probes=activations=timers=closes=posts=0;onFindWindow={};onProbe={};onPos={};jobs.clear();logs.clear();
 gameWindow=(HWND)2;g_hwnd=(HWND)3;g_webviewReady=selfForeground=true;g_exitRequested=false;g_inputReady=true;g_manualPausedPid=0;
 g_powerLifecycle=PowerLifecycle::Ready;generation=1;g_resumeFocusMailbox={};g_resumeFocusPending={};
 g_resumeFocusPostDeferred=false;g_resumeFocusRetryOnMessageAt=0;
 notification=suspendSerial=sleepBoundary=0;publication=1;notificationPending=false;publicPowerKnown=true;
 g_resumeFocusLatestTicket=0;g_resumeFocusWorkerBusy=false;g_wakeFocusNudgeTarget={};g_wakeFocusNudgePower=powerOrigin();
}
static void queue(UINT delay=0){assert(focusQueueResumedGame(42,123,powerOrigin(),delay)==ResumeFocusDelivery::Posted);focusResumedGameOnUiThread();}
static void run(){assert(jobs.size()==1);auto j=std::move(jobs.front());jobs.pop_front();inWorker=true;j();inWorker=false;assert(!g_resumeFocusWorkerBusy);}
static bool status(const char* expected){for(const auto& l:logs)if(l.value("stage","")=="complete"&&l.value("gameStatus","")==expected)return true;return false;}
int main(){
#ifndef SLEEP_FOCUS_ONLY
 reset();responds=false;assert(refocusWebView(false));assert(probes==0&&activations==1);pass("YMCC summon stays on its own input queue even with a hung foreground game");
 reset();g_webviewReady=false;assert(!refocusWebView(false));assert(probes==0&&activations==0);pass("summon preserves startup render readiness gate");
#endif
 reset();queue();assert(probes==0&&activations==0);run();assert(probes==1&&activations==1&&closes==1);pass("UI only queues; responsive exact instance activates on worker");
 reset();responds=false;queue();run();assert(probes==1&&activations==0&&status("window-unresponsive-or-probe-failed"));pass("hung window never activated and worker releases busy flag");
 reset();exited=true;queue();run();assert(probes==0&&activations==0);pass("exited game is optional failure");
 reset();created=456;queue();run();assert(probes==0&&activations==0);pass("PID reuse rejected by retained process creation identity");
 reset();openFails=true;queue();run();assert(probes==0&&activations==0);pass("access denied game never stalls UI");
 reset();gameWindow=nullptr;queue();run();assert(probes==0&&activations==0&&status("no-game-window"));pass("windowless/crashed game safely skipped");
 for(int kind=0;kind<3;++kind){reset();if(kind==0)g_manualPausedPid=42;if(kind==1)manualMarker=true;if(kind==2)sleepMarker=true;queue();run();assert(probes==0&&activations==0&&status("pause-owner-held"));}pass("manual owner/marker and held sleep marker veto automatic nudge");
 reset();queue();generation=2;run();assert(probes==0&&activations==0&&status("stale-power-generation"));pass("queued work canceled by new sleep generation");
 reset();queue();g_exitRequested=true;run();assert(probes==0&&activations==0&&status("exiting"));pass("exit cancels queued work");
 reset();g_powerLifecycle=PowerLifecycle::Resuming;queue(1000);assert(jobs.empty()&&timers==1);now+=1000;focusResumeFocusTimerTickOnUiThread();assert(jobs.empty());g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=false;focusResumeFocusTimerTickOnUiThread();assert(jobs.empty());g_inputReady=true;focusResumeFocusTimerTickOnUiThread();run();pass("focus waits for power Ready AND input Ready AND settle deadline");
 reset();g_powerLifecycle=PowerLifecycle::Resuming;queue();now+=8000;focusResumeFocusTimerTickOnUiThread();assert(jobs.empty()&&status("deadline-expired"));pass("readiness wait has an eight-second terminal deadline");
 reset();queueAccepts=false;queue();assert(jobs.empty()&&!g_resumeFocusWorkerBusy&&timers==1);queueAccepts=true;focusResumeFocusTimerTickOnUiThread();run();pass("busy IPC pool cannot block UI; bounded scheduling retry");
 reset();queueThrows=true;bool escaped=false;try{queue();}catch(const std::runtime_error&){escaped=true;}
 std::cout<<"FOCUS_SUBMISSION_FAULT escaped="<<escaped<<" busy="<<g_resumeFocusWorkerBusy.load()<<" timers="<<timers<<std::endl;
 assert(!escaped&&jobs.empty()&&!g_resumeFocusWorkerBusy&&timers==1);
 queueThrows=false;focusResumeFocusTimerTickOnUiThread();run();assert(activations==1);pass("pool submission exception returns the original focus admission");
 reset();focusQueueResumedGame(42,123,powerOrigin(),1000);focusQueueResumedGame(42,123,powerOrigin(),1000);focusResumedGameOnUiThread();auto ticket=g_resumeFocusPending.ticket;focusResumedGameOnUiThread();assert(g_resumeFocusPending.ticket==ticket);now+=1000;focusResumeFocusTimerTickOnUiThread();run();assert(activations==1);pass("coalesced mailbox + duplicate posts preserve pending readiness request");
 reset();queue();focusQueueResumedGame(42,123,powerOrigin(),1000);focusResumedGameOnUiThread();run();assert(probes==0&&status("superseded"));now+=1000;focusResumeFocusTimerTickOnUiThread();run();assert(activations==1);pass("old queued worker is superseded without dropping the newest request");
 for(int kind=0;kind<5;++kind){reset();queue();onProbe=[kind]{if(kind==0)generation=2;if(kind==1)windowPid=43;if(kind==2)exited=true;if(kind==3)manualMarker=true;if(kind==4)g_inputReady=false;};run();assert(probes==1&&activations==0&&status("canceled-or-target-changed-after-probe"));}pass("generation/window/exit/pause/input races after probe cannot activate");
 reset();queue();onProbe=[]{++notification;notificationPending=true;};run();
 assert(probes==1&&activations==0);pass("public suspend notice after window probe vetoes unchanged private Ready");
 reset();queue();onPos=[]{++notification;};run();assert(probes==1&&activations==0&&status("canceled-or-target-changed-after-probe"));pass("public revocation between async z-order and activation vetoes foreground");
 for(int kind=0;kind<3;++kind){reset();queue();if(kind==0)++notification;if(kind==1)++suspendSerial;if(kind==2)publicPowerKnown=false;run();assert(probes==0&&activations==0&&status("stale-power-generation"));}pass("same-generation notice, suspend serial, and unknown authority retire queued work");
 reset();queue(1000);++notification;now+=1000;focusResumeFocusTimerTickOnUiThread();assert(jobs.empty()&&!g_resumeFocusPending.ticket);queue();run();assert(activations==1);pass("old settle timer cancels; a fresh original request still activates");
 reset();queue();onProbe=[]{++publication;};run();assert(activations==1);pass("publication-only refresh does not retire an admitted wake");
 reset();auto old=powerOrigin();++suspendSerial;focusSingleResumedGame({42},123,old);assert(posts==0);pass("thaw completion cannot submit under a newer public scope");
 reset();focusSingleResumedGame({42,42},123,powerOrigin());assert(posts==1&&g_resumeFocusMailbox.processCreated==123);focusResumedGameOnUiThread();now+=1000;focusResumeFocusTimerTickOnUiThread();run();pass("sleep creation and public origin carried through exact production dispatch");
 reset();sleepBoundary=notification=4;const auto thawPower=powerOrigin();++notification;g_inputReady=false;
 focusSingleResumedGame({42},123,thawPower,sleepBoundary);
 const auto thawTicket=g_resumeFocusMailbox.ticket;const auto thawDeadline=g_resumeFocusMailbox.deadline;
 assert(posts==1&&thawTicket&&thawDeadline==18000);focusResumedGameOnUiThread();
 ++notification;now+=1000;focusResumeFocusTimerTickOnUiThread();
 assert(jobs.empty()&&g_resumeFocusPending.ticket==thawTicket&&g_resumeFocusPending.deadline==thawDeadline);
 now+=250;g_inputReady=true;focusResumeFocusTimerTickOnUiThread();run();
 assert(probes==1&&activations==1&&!g_resumeFocusPending.ticket&&posts==1);
 pass("confirmed thaw follows late same-wake notices under its original ticket and deadline");
 const auto submitThaw=[] {
  focusSingleResumedGame({42},123,powerOrigin(),sleepBoundary);
  const auto admitted=g_resumeFocusMailbox;
  assert(admitted.ticket&&admitted.deadline==now+8000);
  focusResumedGameOnUiThread();now+=1000;focusResumeFocusTimerTickOnUiThread();
  assert(jobs.size()==1);
  return admitted;
 };
 reset();g_wakeFocusNudgeTarget={true,42,123};
 const auto queuedThaw=submitThaw();++notification;run();
 now+=4000;wakeFocusNudgeRunOnUiThread();
 assert(probes==1&&activations==1&&closes==1&&posts==1);
 assert(g_resumeFocusLatestTicket==queuedThaw.ticket&&!g_resumeFocusPending.ticket&&!g_resumeFocusMailbox.ticket);
 pass("a late notice between UI dispatch and worker start retains the confirmed thaw focus");
 reset();submitThaw();onFindWindow=[] {++notification;};run();
 assert(probes==1&&activations==1&&closes==1&&posts==1);
 pass("confirmed thaw binds the latest same-wake notice before probing a discovered window");
 reset();const auto returningThaw=submitThaw();postsSucceed=false;
 onFindWindow=[] {++notification;g_powerLifecycle=PowerLifecycle::Resuming;g_inputReady=false;};run();
 assert(probes==0&&activations==0&&closes==1&&posts==2&&g_resumeFocusPostDeferred);
 assert(g_resumeFocusMailbox.ticket==returningThaw.ticket&&g_resumeFocusMailbox.deadline==returningThaw.deadline);
 fixtureWindowMessage(WM_NULL);assert(jobs.empty()&&g_resumeFocusPending.ticket==returningThaw.ticket);
 assert(g_resumeFocusPending.notBefore==returningThaw.notBefore&&g_resumeFocusPending.deadline==returningThaw.deadline);
 onFindWindow={};now+=250;++notification;g_powerLifecycle=PowerLifecycle::Ready;g_inputReady=true;
 focusResumeFocusTimerTickOnUiThread();run();focusResumedGameOnUiThread();
 assert(probes==1&&activations==1&&closes==2&&jobs.empty()&&g_resumeFocusLatestTicket==returningThaw.ticket);
 pass("zero-probe worker releases its process and returns the same ticket through the existing UI fallback");
 for(int kind=0;kind<3;++kind){
  reset();const auto admitted=submitThaw();++notification;g_inputReady=false;run();
  assert(probes==0&&closes==0&&g_resumeFocusMailbox.ticket==admitted.ticket);
  if(kind==0)now=admitted.deadline;else if(kind==1)++sleepBoundary;else g_exitRequested=true;
  focusResumedGameOnUiThread();g_inputReady=true;focusResumeFocusTimerTickOnUiThread();
  assert(jobs.empty()&&probes==0&&activations==0&&!g_resumeFocusPending.ticket&&!g_resumeFocusMailbox.ticket);
  assert(status(kind==0?"deadline-expired":kind==1?"stale-power-generation":"exiting"));
 }pass("returned thaw focus cannot survive its deadline, a new sleep edge or exit");
 for(int kind=0;kind<4;++kind){
  reset();submitThaw();++notification;
  if(kind==0)notificationPending=true;else if(kind==1)++suspendSerial;else if(kind==2)manualMarker=true;else created=456;
  run();assert(probes==0&&activations==0&&posts==1&&!g_resumeFocusMailbox.ticket);
 }pass("late-notice continuation never probes across pending suspend, new serial, manual pause or reused PID");
 reset();const auto olderThaw=submitThaw();
 onFindWindow=[] {
  ++notification;g_inputReady=false;
  assert(focusQueueResumedGame(42,123,powerOrigin(),0)==ResumeFocusDelivery::Posted);
 };run();
 assert(status("superseded")&&probes==0&&g_resumeFocusMailbox.ticket>olderThaw.ticket&&posts==2);
 onFindWindow={};g_inputReady=true;focusResumedGameOnUiThread();run();assert(probes==1&&activations==1);
 pass("a newer request arriving during discovery keeps the mailbox and exclusively probes");
 for(int kind=0;kind<4;++kind){
  reset();const auto origin=powerOrigin();
  if(kind==0)++sleepBoundary;else if(kind==1)notificationPending=true;else if(kind==2)++suspendSerial;else ++generation;
  focusSingleResumedGame({42},123,origin,0);assert(posts==0);
 }pass("confirmed thaw cannot submit across an observed sleep edge, pending suspend, serial or generation");
 for(int kind=0;kind<4;++kind){
  reset();focusSingleResumedGame({42},123,powerOrigin(),0);focusResumedGameOnUiThread();++notification;
  if(kind==0)++sleepBoundary;else if(kind==1)notificationPending=true;else if(kind==2)++suspendSerial;else ++generation;
  now+=1000;focusResumeFocusTimerTickOnUiThread();
  assert(jobs.empty()&&!g_resumeFocusPending.ticket&&status("stale-power-generation"));
 }pass("parked thaw focus retires when a new sleep edge wins before UI dispatch");
 reset();focusSingleResumedGame({42},123,powerOrigin(),0);focusResumedGameOnUiThread();
 ++notification;now+=8000;focusResumeFocusTimerTickOnUiThread();
 assert(jobs.empty()&&!g_resumeFocusPending.ticket&&status("deadline-expired"));
 pass("a late notice cannot extend confirmed thaw focus past its deadline");
 for(bool afterZOrder:{false,true}){
  reset();focusSingleResumedGame({42},123,powerOrigin(),0);focusResumedGameOnUiThread();
  auto notify=[] {++notification;};if(afterZOrder)onPos=notify;else onProbe=notify;
  now+=1000;focusResumeFocusTimerTickOnUiThread();run();
  assert(probes==1&&activations==0&&status("canceled-or-target-changed-after-probe"));
 }pass("an already dispatched thaw worker does not follow notices after its probe or async window action");
 reset();focusSingleResumedGame({42,43},0,powerOrigin());assert(posts==0);
 assert(focusQueueResumedGame(0,123,powerOrigin(),0)==ResumeFocusDelivery::Rejected);
 assert(focusQueueResumedGame(42,0,powerOrigin(),0)==ResumeFocusDelivery::Rejected);pass("ambiguous or incomplete identity never posted");
 reset();postsSucceed=false;assert(focusQueueResumedGame(42,123,powerOrigin(),0)==ResumeFocusDelivery::Deferred);
 const auto deferredTicket=g_resumeFocusMailbox.ticket;
 assert(deferredTicket&&logs.back().value("gameStatus","")=="ui-post-deferred");
 fixtureWindowMessage(WM_POWERBROADCAST);fixtureWindowMessage(kSharedPowerStateMessage);fixtureWindowMessage(WM_CLOSE);
 fixtureWindowMessage(WM_FAN_KERNEL_POWER_FACT);fixtureWindowMessage(WM_TIMER);
 assert(jobs.empty()&&g_resumeFocusMailbox.ticket==deferredTicket);
 fixtureWindowMessage(WM_NULL);focusResumedGameOnUiThread();run();
 assert(!g_resumeFocusMailbox.ticket&&activations==1);pass("failed UI post is delivered once from an ordinary window message");
 for(int kind=0;kind<3;++kind){
  reset();postsSucceed=false;assert(focusQueueResumedGame(42,123,powerOrigin(),0)==ResumeFocusDelivery::Deferred);
  if(kind==0)now+=8000;else if(kind==1)++suspendSerial;else g_exitRequested=true;
  fixtureWindowMessage(WM_NULL);
  assert(jobs.empty()&&!g_resumeFocusMailbox.ticket&&status(kind==0?"deadline-expired":kind==1?"stale-power-generation":"exiting"));
 }pass("failed UI post can still end at its original deadline, newer sleep or exit");
 reset();timersSucceed=false;queue(1000);assert(timers==1&&jobs.empty());
 now+=249;fixtureWindowMessage(WM_NULL);assert(timers==1&&jobs.empty());
 now+=751;fixtureWindowMessage(WM_NULL);run();assert(!g_resumeFocusRetryOnMessageAt&&activations==1);
 pass("failed timer keeps the original settle window and retries on an ordinary message");
 reset();postsSucceed=false;logThrows=true;assert(focusQueueResumedGame(42,123,powerOrigin(),0)==ResumeFocusDelivery::Deferred);
 fixtureWindowMessage(WM_NULL);run();assert(activations==1&&closes==1);pass("diagnostic failures cannot interrupt focus delivery or cleanup");
 reset();g_wakeFocusNudgeTarget={true,42,123};wakeFocusNudgeRunOnUiThread();assert(posts==1);focusResumedGameOnUiThread();run();assert(activations==1);pass("wake timer uses captured identity and shared bounded worker");
 reset();g_wakeFocusNudgeTarget={true,42,123};postsSucceed=false;wakeFocusNudgeRunOnUiThread();
 assert(logs.back().value("action","")=="deferred");fixtureWindowMessage(WM_INPUT);run();assert(activations==1);
 pass("wake nudge distinguishes deferred delivery from a skipped target");
 reset();g_wakeFocusNudgeTarget={true,42,123};generation=2;wakeFocusNudgeRunOnUiThread();assert(posts==0);pass("stale five-second wake timer cannot queue focus into new generation");
 reset();g_wakeFocusNudgeTarget={true,42,123};++notification;wakeFocusNudgeRunOnUiThread();assert(posts==0);pass("five-second nudge cannot borrow a newer notice within the same generation");
 reset();g_wakeFocusNudgeTarget={true,42,123};created=456;wakeFocusNudgeRunOnUiThread();focusResumedGameOnUiThread();run();assert(probes==0&&activations==0);pass("delayed nudge cannot substitute a restarted game's reused PID");
 reset();wakeFocusNudgeRunOnUiThread();assert(posts==0);pass("no captured game means no automatic foreground guessing");
 for(const auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Resuming}){reset();g_powerLifecycle=phase;queue();assert(jobs.empty());now+=8000;focusResumeFocusTimerTickOnUiThread();assert(status("deadline-expired"));}pass("all unsafe power phases defer without holding a worker");
 reset();queue();onProbe=[] {throw std::runtime_error("fixture exception");};run();assert(status("worker-exception")&&!g_resumeFocusWorkerBusy);pass("probe exception releases single-flight slot and emits terminal status");
 std::cout<<"resume focus production selftest: "<<passed<<" groups PASS\n";
}
"""
cpp=out/'resume_game_focus_native_selftest.cpp'
summon = '#define SLEEP_FOCUS_ONLY\n' if args.sleep_focus_only else function('refocusWebView')
cpp.write_text(preamble+'\n'+constants+'\n'+globals+'\n'+summon+'\n'+'\n'.join(map(function,names))+
               '\nstatic void fixtureWindowMessage(UINT m){'+message_route.group(0)+'}\n'+checks,'utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
assert vs.is_file()
cmd=out/'run-focus-selftest.cmd'
lines=['@echo off',f'call "{vs}" >nul','if not "%errorlevel%"=="0" exit /b 1']
for src,stem in [(cpp,'resume_game_focus_native_selftest'),(repo/'tools/resume_game_focus_win32_smoketest.cpp','resume_game_focus_win32_smoketest')]:
    exe=out/(stem+'.exe')
    lines += [f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{src}" /I"{repo / "native"}" /I"{repo / "deps/json"}" /Fo"{out/(stem+".obj")}" /Fe"{exe}" /link user32.lib',
              'if not "%errorlevel%"=="0" exit /b 1',f'"{exe}"','if not "%errorlevel%"=="0" exit /b 1']
cmd.write_text('\r\n'.join(lines)+'\r\n','ascii')
result=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'resume-focus-selftest.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8',errors='replace'))
raise SystemExit(result.returncode or (b'Assertion failed:' in result.stdout))
