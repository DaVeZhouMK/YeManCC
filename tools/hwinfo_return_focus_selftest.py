"""Compile real controller return-focus functions against deterministic Win32 stubs.
No real window is shown/hidden, no game/input/driver/monitor process is touched.
"""
from pathlib import Path
import argparse
import re
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('--output', type=Path, default=Path('../../Build/Validation/HWiNFO-ReturnFocus-20261009'))
args = parser.parse_args()
repo = Path(__file__).resolve().parent.parent
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=True)
source = (repo / 'native/main.cpp').read_text('utf-8-sig')
masked = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
                lambda m: ' ' * len(m.group()), source, flags=re.S)

def function(name, optional=False):
    match = re.search(r'\bstatic\s+[^;{}]*?\b' + re.escape(name) + r'\s*\([^;{}]*?\)\s*\{', masked)
    if not match and optional:
        return ''
    assert match, f'missing production function: {name}'
    start = masked.index('{', match.start())
    depth, end = 1, start + 1
    while depth:
        depth += (masked[end] == '{') - (masked[end] == '}')
        end += 1
    return source[match.start():end]

preamble = r'''
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cwctype>
#include <iostream>
#include <map>
#include <string>
#include <vector>
#include "monitor_identity.h"
struct FocusTargetSnapshot {
 HWND hwnd=nullptr; DWORD pid=0; ULONGLONG processCreated=0;
 std::wstring path, className, monitorDevice; RECT monitorRect{};
 bool fullscreen=false, valid=false;
};
struct FocusSessionState {
 FocusTargetSnapshot target; bool active=false,returning=false,ownedTopmost=false;
 ULONGLONG returnStarted=0,returnDeadline=0;
};
struct Window {
 DWORD pid; HWND root; bool visible=true,iconic=false,tool=false,cloaked=false;
};
static HWND handle(int n){return reinterpret_cast<HWND>(static_cast<INT_PTR>(n));}
static std::map<HWND,Window> windows;
static std::map<DWORD,std::wstring> paths;
static HWND g_hwnd=handle(1), foreground=nullptr;
static FocusSessionState g_focusSession;
static FocusTargetSnapshot remembered;
static std::atomic<DWORD> g_manualPausedPid{0};
static int g_summonFocusRetries=0;
static bool g_summonAltTried=false,blocked=false;
static constexpr UINT_PTR SUMMON_FOCUS_TIMER_ID=1,RETURN_GAME_FOCUS_TIMER_ID=2;
static int showCalls=0,restoreCalls=0,activations=0,findWindows=0,findProcesses=0,timers=0;
static BOOL fakeIsWindow(HWND h){return windows.count(h)!=0;}
static BOOL fakeVisible(HWND h){return fakeIsWindow(h)&&windows.at(h).visible;}
static BOOL fakeIconic(HWND h){return fakeIsWindow(h)&&windows.at(h).iconic;}
static HWND fakeAncestor(HWND h,UINT){return fakeIsWindow(h)?windows.at(h).root:nullptr;}
static DWORD fakeThread(HWND h,DWORD* pid){if(pid)*pid=fakeIsWindow(h)?windows.at(h).pid:0;return 1;}
static DWORD fakeCurrentPid(){return 1;}
static LONG_PTR fakeStyle(HWND h,int){return windows.at(h).tool?WS_EX_TOOLWINDOW:0;}
static HRESULT fakeDwm(HWND h,DWORD,void* v,DWORD){*static_cast<BOOL*>(v)=windows.at(h).cloaked;return S_OK;}
static BOOL fakeRect(HWND,RECT* r){*r={0,0,1000,700};return TRUE;}
static HMONITOR fakeMonitor(HWND,DWORD){return reinterpret_cast<HMONITOR>(1);}
static HWND fakeForeground(){return foreground;}
static BOOL fakeShow(HWND h,int command){
 if(command==SW_SHOW){++showCalls;windows.at(h).visible=true;}
 if(command==SW_RESTORE){++restoreCalls;windows.at(h).iconic=false;}
 return TRUE;
}
static BOOL fakeKill(HWND,UINT_PTR){return TRUE;}
static UINT_PTR fakeTimer(HWND,UINT_PTR id,UINT,TIMERPROC){++timers;return id;}
#define IsWindow fakeIsWindow
#define IsWindowVisible fakeVisible
#define IsIconic fakeIconic
#define GetAncestor fakeAncestor
#define GetWindowThreadProcessId fakeThread
#define GetCurrentProcessId fakeCurrentPid
#define GetWindowLongPtrW fakeStyle
#define DwmGetWindowAttribute fakeDwm
#define GetWindowRect fakeRect
#define MonitorFromWindow fakeMonitor
#define GetForegroundWindow fakeForeground
#define ShowWindowAsync fakeShow
#define KillTimer fakeKill
#define SetTimer fakeTimer
static bool focusQueryProcessIdentity(DWORD pid,std::wstring* path,ULONGLONG* created){
 if(!paths.count(pid))return false;if(path)*path=paths.at(pid);if(created)*created=123;return true;
}
static std::wstring focusWindowClass(HWND){return L"Fixture";}
static bool focusMonitorInfo(HMONITOR,std::wstring* device,RECT* rect){
 if(device)*device=L"FixtureMonitor";if(rect)*rect={0,0,1920,1080};return true;
}
static bool focusWindowLooksFullscreen(HWND,RECT){return false;}
static bool focusGameControlBlocked(){return blocked;}
static bool focusPidIsManuallyPaused(DWORD pid){return pid!=0&&pid==g_manualPausedPid;}
static HWND focusFindWindowForPid(DWORD pid,const FocusTargetSnapshot* =nullptr){
 ++findWindows;for(const auto& [h,w]:windows)if(w.pid==pid)return h;return nullptr;
}
static DWORD focusFindProcessByPath(const std::wstring& path){
 ++findProcesses;for(const auto& [pid,p]:paths)if(_wcsicmp(path.c_str(),p.c_str())==0)return pid;return 0;
}
static bool focusResolveTarget(FocusTargetSnapshot&);
static FocusTargetSnapshot focusRememberedGameTarget(){
 auto candidate=remembered;if(candidate.valid&&focusResolveTarget(candidate))return candidate;return {};
}
struct Game {DWORD pid=0;std::wstring path;};
static Game nativeDetectGame(){return {};}
static void focusReleaseOwnedTopmost(){g_focusSession.ownedTopmost=false;}
static void hideWindowAnimated(HWND h){windows.at(h).visible=false;}
static void focusClearSession(){g_focusSession={};}
namespace ymcc { static void requestWindowForegroundAsync(HWND h){++activations;foreground=h;} }
'''
names = ['sgBaseName', 'focusBackgroundMonitorName', 'focusWindowProcessName', 'focusIsBackgroundMonitorWindow',
         'focusCaptureTarget', 'focusTargetIdentityMatches', 'isUsableFocusWindow',
         'focusIsRestorableExactWindow', 'focusResolveTarget', 'resolvePreviousFocusWindow',
         'focusForegroundMatchesTarget', 'refocusPreviousWindow', 'focusBeginReturnToPreviousWindow']
checks = r'''
static int passed=0,failed=0;
static void check(bool ok,const char* message){
 std::cout<<(ok?"PASS ":"FAIL ")<<message<<"\n";if(ok)++passed;else ++failed;
}
static void reset(){
 windows={{handle(1),{1,handle(1)}},{handle(2),{42,handle(2)}},
          {handle(3),{77,handle(3),false}},{handle(4),{77,handle(3)}},
          {handle(5),{55,handle(5)}}};
 paths={{1,L"C:\\YeManCC.exe"},{42,L"C:\\Games\\Game.exe"},
        {77,L"C:\\Program Files\\HWiNFO64\\HWiNFO64.EXE"},{55,L"C:\\Windows\\notepad.exe"}};
 foreground=handle(1);g_focusSession={};remembered={};blocked=false;g_manualPausedPid=0;
 showCalls=restoreCalls=activations=findWindows=findProcesses=timers=0;
}
static FocusTargetSnapshot snapshot(HWND h){
 FocusTargetSnapshot t;t.hwnd=h;t.pid=windows.at(h).pid;t.path=paths.at(t.pid);t.processCreated=123;t.valid=true;return t;
}
static bool noActivation(){return showCalls==0&&restoreCalls==0&&activations==0;}
int main(){
 reset();check(!focusCaptureTarget(handle(4)).valid,"visible HWiNFO sensors must not capture its hidden root-owner window");
 reset();check(!focusCaptureTarget(handle(3)).valid,"hidden HWiNFO main window is never a captured return target");
 reset();g_focusSession.target=snapshot(handle(3));focusBeginReturnToPreviousWindow(1000);
 check(!windows.at(handle(1)).visible&&noActivation(),"double-B hides YMCC without showing/activating cached hidden HWiNFO");
 reset();windows.at(handle(3)).visible=true;windows.at(handle(3)).iconic=true;
 g_focusSession.target=snapshot(handle(3));refocusPreviousWindow();
 check(noActivation(),"minimized HWiNFO must not be restored");
 reset();windows.at(handle(3)).visible=true;g_focusSession.target=snapshot(handle(3));refocusPreviousWindow();
 check(noActivation(),"even visible HWiNFO must not receive automatic return-focus");
 reset();g_focusSession.target=snapshot(handle(3));g_focusSession.target.hwnd=nullptr;refocusPreviousWindow();
 check(noActivation()&&findWindows==0&&findProcesses==0,"stale HWiNFO snapshot cannot reacquire a new window/process");
 reset();g_focusSession.target=snapshot(handle(3));g_focusSession.target.path.clear();refocusPreviousWindow();
 check(noActivation(),"live HWND/PID rejects HWiNFO even with an empty stored path");
 for(const auto* name:{L"HWiNFO.exe",L"HWiNFO32.exe",L"HwInFo64.ExE"}){
  reset();paths.at(77)=std::wstring(L"C:\\Portable\\")+name;
  g_focusSession.target=snapshot(handle(3));refocusPreviousWindow();
  check(noActivation(),"legacy/32-bit/64-bit HWiNFO names are excluded case-insensitively");
 }
 reset();check(!isUsableFocusWindow(handle(4),77,true)&&!focusIsRestorableExactWindow(handle(3),77),
              "both window-selection gates reject HWiNFO");
 reset();g_focusSession.target=snapshot(handle(3));remembered=snapshot(handle(2));refocusPreviousWindow();
 check(activations==1&&foreground==handle(2)&&showCalls==0,"rejected HWiNFO can fall back to the already remembered real game");
 reset();g_focusSession.target=snapshot(handle(2));focusBeginReturnToPreviousWindow(1000);
 check(!windows.at(handle(1)).visible&&activations==1&&foreground==handle(2),"normal double-B return-to-game behavior stays intact");
 reset();windows.at(handle(2)).iconic=true;g_focusSession.target=snapshot(handle(2));refocusPreviousWindow();
 check(restoreCalls==1&&activations==1,"minimized real game can still be restored");
 reset();windows.at(handle(2)).visible=false;g_focusSession.target=snapshot(handle(2));refocusPreviousWindow();
 check(showCalls==1&&activations==1,"legitimate hidden real game return remains supported");
 reset();check(focusCaptureTarget(handle(5)).valid,"normal desktop application capture is not blocked");
 g_focusSession.target=snapshot(handle(5));refocusPreviousWindow();
 check(activations==1&&foreground==handle(5),"normal desktop application return remains supported");
 reset();remembered=snapshot(handle(3));refocusPreviousWindow();
 check(noActivation(),"remembered-target fallback cannot activate HWiNFO either");
 reset();focusBeginReturnToPreviousWindow(1000);
 check(!windows.at(handle(1)).visible&&noActivation(),"first startup with no previous target only hides YMCC");
 reset();g_focusSession.target=snapshot(handle(2));g_manualPausedPid=42;refocusPreviousWindow();
 check(noActivation(),"manually paused game is not activated");
 std::cout<<"production return-focus regression: "<<passed<<" PASS, "<<failed<<" FAIL\n";
 return failed?1:0;
}
'''
cpp = out / 'hwinfo_return_focus_native_selftest.cpp'
cpp.write_text(preamble + '\n' + '\n'.join(function(n, optional=n.startswith('focusBackground') or n == 'focusIsBackgroundMonitorWindow') for n in names) + '\n' + checks, 'utf-8')
vs = Path(r'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat')
assert vs.is_file(), f'compiler setup missing: {vs}'
exe = out / 'hwinfo_return_focus_native_selftest.exe'
cmd = out / 'run-hwinfo-return-focus-selftest.cmd'
cmd.write_text('\r\n'.join([
    '@echo off', f'call "{vs}" >nul', 'if errorlevel 1 exit /b 1',
    f'cl /nologo /std:c++20 /utf-8 /EHsc /MT /W3 "{cpp}" /I"{repo / "native"}" /Fo"{out / "selftest.obj"}" /Fe"{exe}"',
    'if errorlevel 1 exit /b 1', f'"{exe}"', 'exit /b %errorlevel%',
]) + '\r\n', 'ascii')
result = subprocess.run(['cmd.exe', '/d', '/c', str(cmd)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
(out / 'selftest.log').write_bytes(result.stdout)
print(result.stdout.decode('utf-8', errors='replace'))
raise SystemExit(result.returncode)