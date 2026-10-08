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
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
repo = Path.cwd()
source = (repo/'native/main.cpp').read_text('utf-8-sig')
masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                lambda m: ' '*len(m.group()), source, flags=re.S)

def function(name):
    match = re.search(r'\bstatic\s+[^;{}]*?\b'+re.escape(name)+r'\s*\([^;{}]*?\)\s*\{', masked)
    assert match, f'missing function {name}'
    opening = masked.index('{', match.start())
    depth, end = 1, opening+1
    while depth:
        depth += (masked[end]=='{')-(masked[end]=='}')
        end += 1
    return source[match.start():end]

assert 'AttachThreadInput(' not in masked, 'no YMCC input queue may attach to a foreign game'
assert 'focusSingleResumedGame(rr.pids, target.processCreated, expectedGeneration)' in source
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
static bool inWorker=false, queueAccepts=true, postsSucceed=true;
static int probes=0, activations=0, timers=0, closes=0, posts=0;
static std::function<void()> onProbe;
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
static BOOL fakePos(HWND,HWND,int,int,int,int,UINT flags){assert(inWorker&&(flags&SWP_ASYNCWINDOWPOS));return TRUE;}
static BOOL fakeForeground(HWND h){assert(inWorker||h==(HWND)3);++activations;return TRUE;}
static BOOL fakeIsWindow(HWND h){return h!=nullptr;}
static BOOL fakeBring(HWND h){assert(h==(HWND)3);return TRUE;}
static HWND fakeActive(HWND h){assert(h==(HWND)3);return nullptr;}
static HWND fakeFocus(HWND h){assert(h==(HWND)3);return nullptr;}
static UINT_PTR fakeTimer(HWND,UINT_PTR id,UINT,TIMERPROC){++timers;return id;}
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
static unsigned long long currentPowerGeneration(){return generation;}
struct FocusTargetSnapshot { bool valid=false; DWORD pid=0; ULONGLONG processCreated=0; };
static FocusTargetSnapshot g_wakeFocusNudgeTarget;
static std::atomic<unsigned long long> g_wakeFocusNudgeGeneration{1};
static constexpr UINT kWakeFocusNudgeDelayMs=5000;
static const std::wstring SG_SLEEP_LEASE_DIR=L"sleep",SG_MANUAL_DIR=L"manual";
namespace fspath {
 static bool exists(const std::wstring& p){return p.find(L"manual")==0?manualMarker:sleepMarker;}
 static bool exists(const std::wstring& p,std::error_code& ec){ec.clear();return exists(p);}
}
static ULONGLONG nativeProcessCreatedFromHandle(HANDLE){return created;}
static HWND focusFindWindowForPid(DWORD){return gameWindow;}
static bool focusQueryProcessIdentity(DWORD,std::wstring*,ULONGLONG* value){*value=created;return !exited;}
static void appendNativeLifecycleLog(const char* event,json value){value["event"]=event;logs.push_back(value);}
template<class...T>static void traceLog(const char*,T...){}
#define RESUME_GAME_FOCUS_TIMER_ID 0xA211
#define WM_SG_RESUME_GAME_FOCUS (WM_USER+18)
static bool poolSubmit(std::function<void()> job){if(!queueAccepts)return false;jobs.push_back(std::move(job));return true;}
"""
begin=source.index('static std::mutex g_resumeFocusMailboxMx;')
end=source.index('static ymcc::ResumeFocusGate focusResumeGate(',begin)
globals=source[begin:end]
names=['focusPidIsManuallyPaused','focusGameControlBlocked','refocusWebView','focusResumeGate','focusResumeLog',
       'focusResumeTargetPaused','focusResumeGameWorker','focusResumeFocusTimerTickOnUiThread',
       'focusResumedGameOnUiThread','focusQueueResumedGame','focusSingleResumedGame','wakeFocusNudgeRunOnUiThread']
checks=r"""
static int passed=0;
static void pass(const char* s){++passed;std::cout<<"PASS "<<s<<"\n";}
static void reset(){
 now=10000;created=123;windowPid=42;exited=openFails=manualMarker=sleepMarker=inWorker=false;
 responds=queueAccepts=postsSucceed=true;probes=activations=timers=closes=posts=0;onProbe={};jobs.clear();logs.clear();
 gameWindow=(HWND)2;g_hwnd=(HWND)3;g_webviewReady=selfForeground=true;g_exitRequested=false;g_inputReady=true;g_manualPausedPid=0;
 g_powerLifecycle=PowerLifecycle::Ready;generation=1;g_resumeFocusMailbox={};g_resumeFocusPending={};
 g_resumeFocusLatestTicket=0;g_resumeFocusWorkerBusy=false;g_wakeFocusNudgeTarget={};g_wakeFocusNudgeGeneration=1;
}
static void queue(UINT delay=0){assert(focusQueueResumedGame(42,123,1,delay));focusResumedGameOnUiThread();}
static void run(){assert(jobs.size()==1);auto j=std::move(jobs.front());jobs.pop_front();inWorker=true;j();inWorker=false;assert(!g_resumeFocusWorkerBusy);}
static bool status(const char* expected){for(const auto& l:logs)if(l.value("stage","")=="complete"&&l.value("gameStatus","")==expected)return true;return false;}
int main(){
 reset();responds=false;assert(refocusWebView(false));assert(probes==0&&activations==1);pass("YMCC summon stays on its own input queue even with a hung foreground game");
 reset();g_webviewReady=false;assert(!refocusWebView(false));assert(probes==0&&activations==0);pass("summon preserves startup render readiness gate");
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
 reset();focusQueueResumedGame(42,123,1,1000);focusQueueResumedGame(42,123,1,1000);focusResumedGameOnUiThread();auto ticket=g_resumeFocusPending.ticket;focusResumedGameOnUiThread();assert(g_resumeFocusPending.ticket==ticket);now+=1000;focusResumeFocusTimerTickOnUiThread();run();assert(activations==1);pass("coalesced mailbox + duplicate posts preserve pending readiness request");
 reset();queue();focusQueueResumedGame(42,123,1,1000);focusResumedGameOnUiThread();run();assert(probes==0&&status("superseded"));now+=1000;focusResumeFocusTimerTickOnUiThread();run();assert(activations==1);pass("old queued worker is superseded without dropping the newest request");
 for(int kind=0;kind<5;++kind){reset();queue();onProbe=[kind]{if(kind==0)generation=2;if(kind==1)windowPid=43;if(kind==2)exited=true;if(kind==3)manualMarker=true;if(kind==4)g_inputReady=false;};run();assert(probes==1&&activations==0&&status("canceled-or-target-changed-after-probe"));}pass("generation/window/exit/pause/input races after probe cannot activate");
 reset();focusSingleResumedGame({42,42},123,1);assert(posts==1&&g_resumeFocusMailbox.processCreated==123);focusResumedGameOnUiThread();now+=1000;focusResumeFocusTimerTickOnUiThread();run();pass("sleep creation+generation carried through exact production dispatch");
 reset();focusSingleResumedGame({42,43});assert(posts==0);assert(!focusQueueResumedGame(0,123,1,0));assert(!focusQueueResumedGame(42,0,1,0));pass("ambiguous or incomplete identity never posted");
 reset();postsSucceed=false;assert(!focusQueueResumedGame(42,123,1,0));assert(status("ui-post-failed"));pass("failed UI post has a terminal diagnostic");
 reset();g_wakeFocusNudgeTarget={true,42,123};wakeFocusNudgeRunOnUiThread();assert(posts==1);focusResumedGameOnUiThread();run();assert(activations==1);pass("wake timer uses captured identity and shared bounded worker");
 reset();g_wakeFocusNudgeTarget={true,42,123};generation=2;wakeFocusNudgeRunOnUiThread();assert(posts==0);pass("stale five-second wake timer cannot queue focus into new generation");
 reset();g_wakeFocusNudgeTarget={true,42,123};created=456;wakeFocusNudgeRunOnUiThread();focusResumedGameOnUiThread();run();assert(probes==0&&activations==0);pass("delayed nudge cannot substitute a restarted game's reused PID");
 reset();wakeFocusNudgeRunOnUiThread();assert(posts==0);pass("no captured game means no automatic foreground guessing");
 for(const auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Resuming}){reset();g_powerLifecycle=phase;queue();assert(jobs.empty());now+=8000;focusResumeFocusTimerTickOnUiThread();assert(status("deadline-expired"));}pass("all unsafe power phases defer without holding a worker");
 reset();queue();onProbe=[] {throw std::runtime_error("fixture exception");};run();assert(status("worker-exception")&&!g_resumeFocusWorkerBusy);pass("probe exception releases single-flight slot and emits terminal status");
 std::cout<<"resume focus production selftest: "<<passed<<" groups PASS\n";
}
"""
cpp=out/'resume_game_focus_native_selftest.cpp'
cpp.write_text(preamble+'\n'+globals+'\n'+'\n'.join(map(function,names))+'\n'+checks,'utf-8')
vs=Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
assert vs.is_file()
cmd=out/'run-focus-selftest.cmd'
lines=['@echo off',f'call "{vs}" >nul','if errorlevel 1 exit /b 1']
for src,stem in [(cpp,'resume_game_focus_native_selftest'),(repo/'tools/resume_game_focus_win32_smoketest.cpp','resume_game_focus_win32_smoketest')]:
    exe=out/(stem+'.exe')
    lines += [f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{src}" /I"{repo / "native"}" /I"{repo / "deps/json"}" /Fo"{out/(stem+".obj")}" /Fe"{exe}" /link user32.lib',
              'if errorlevel 1 exit /b 1',f'"{exe}"','if errorlevel 1 exit /b 1']
cmd.write_text('\r\n'.join(lines)+'\r\n','ascii')
result=subprocess.run(['cmd.exe','/d','/c',str(cmd)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
(out/'resume-focus-selftest.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8',errors='replace'))
raise SystemExit(result.returncode)
