// Only fake windows/processes exist in this executable. The generated includes
// contain the actual main.cpp entry points, IPC lambda and timer consumer.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cwctype>
#include <deque>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>
#include <json.hpp>
using json = nlohmann::json;
#include "monitor_identity.h"
#include "production_focus_types.inc"
static HWND h(int n) { return reinterpret_cast<HWND>(static_cast<INT_PTR>(n)); }
struct Window { DWORD pid; bool visible=true, iconic=false, tool=false, cloaked=false; std::wstring cls=L"Game"; };
struct Process { std::wstring path; ULONGLONG birth=123; bool alive=true, access=true, query=true, times=true; };
static std::map<HWND,Window> windows;
static std::map<DWORD,Process> processes;
static HWND g_hwnd=h(1), foreground=h(1), afterHide=h(3);
static ULONGLONG clockMs=10000;
static unsigned long long powerGeneration=1;
static std::atomic<unsigned long long> g_losslessReturnIdentityEpoch{1};
static std::atomic<bool> g_losslessReturnArmed{false};
static std::atomic<bool> g_exitRequested{false};
enum class PowerLifecycle { Ready, Suspending, Suspended, Resuming };
static std::atomic<PowerLifecycle> g_powerLifecycle{PowerLifecycle::Ready};
static std::atomic<DWORD> g_manualPausedPid{0};
static std::mutex g_rememberedGameTargetMx,g_gameValveMx;
static FocusTargetSnapshot g_rememberedGameTarget,g_pendingSummonTarget;
static ULONGLONG g_rememberedGameDeadline=0;
struct Game { DWORD pid=0; ULONGLONG processCreated=0; std::wstring path; };
struct Valve { Game game; };
static Valve g_gameValveTarget;
static FocusSessionState g_focusSession;
static int g_summonFocusRetries=0,g_asyncMode=2;
static bool g_summonAltTried=false,g_bClosePending=false;
static ULONGLONG g_bClosePendingSince=0,g_bCloseReadyAt=0;
static constexpr UINT_PTR SUMMON_FOCUS_TIMER_ID=1,RETURN_GAME_FOCUS_TIMER_ID=2;
static std::set<UINT_PTR> timers;
static std::deque<UINT_PTR> postedTimers;
static int requests=0,findWindows=0,findProcesses=0,detectCalls=0,opens=0,closes=0,shows=0;
static bool acceptForeground=true,timerWorks=true,blocked=false;
static std::vector<HWND> destinations;
static std::vector<ULONGLONG> requestTimes;
static std::function<void()> onFind,onQuery;
static DWORD processPid(HANDLE p) { return static_cast<DWORD>(reinterpret_cast<UINT_PTR>(p)); }
static HANDLE fakeOpen(DWORD flags,BOOL,DWORD pid) {
    assert((flags&PROCESS_QUERY_LIMITED_INFORMATION)!=0); ++opens;
    if (!processes.count(pid)||!processes.at(pid).access) return nullptr;
    return reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(pid));
}
static DWORD fakeWait(HANDLE p,DWORD timeout) { assert(timeout==0); return processes.at(processPid(p)).alive?WAIT_TIMEOUT:WAIT_OBJECT_0; }
static BOOL fakeClose(HANDLE) { ++closes; return TRUE; }
static BOOL fakeQuery(HANDLE p,DWORD,wchar_t* text,DWORD* size) {
    if(onQuery){auto fn=std::move(onQuery);onQuery={};fn();}
    const auto& proc=processes.at(processPid(p)); if(!proc.query)return FALSE;
    assert(*size>proc.path.size());wcscpy_s(text,*size,proc.path.c_str());*size=static_cast<DWORD>(proc.path.size());return TRUE;
}
static BOOL fakeTimes(HANDLE p,FILETIME* birth,FILETIME*,FILETIME*,FILETIME*) {
    const auto& proc=processes.at(processPid(p));if(!proc.times)return FALSE;
    birth->dwLowDateTime=static_cast<DWORD>(proc.birth);birth->dwHighDateTime=static_cast<DWORD>(proc.birth>>32);return TRUE;
}
static BOOL fakeIsWindow(HWND w) { return windows.count(w)!=0; }
static BOOL fakeVisible(HWND w) { return fakeIsWindow(w)&&windows.at(w).visible; }
static BOOL fakeIconic(HWND w) { return fakeIsWindow(w)&&windows.at(w).iconic; }
static DWORD fakeThread(HWND w,DWORD* pid) { if(pid)*pid=fakeIsWindow(w)?windows.at(w).pid:0;return fakeIsWindow(w)?2:0; }
static HWND fakeAncestor(HWND w,UINT) { return fakeIsWindow(w)?w:nullptr; }
static LONG_PTR fakeStyle(HWND w,int) { return windows.at(w).tool?WS_EX_TOOLWINDOW:0; }
static HRESULT fakeDwm(HWND w,DWORD,void* value,DWORD) { *static_cast<BOOL*>(value)=windows.at(w).cloaked;return S_OK; }
static BOOL fakeRect(HWND,RECT* rect) { *rect={0,0,1920,1080};return TRUE; }
static HMONITOR fakeMonitor(HWND,DWORD) { return reinterpret_cast<HMONITOR>(1); }
static HWND fakeForeground() { return foreground; }
static DWORD fakeCurrentPid() { return 1; }
static ULONGLONG fakeTick() { return clockMs; }
static BOOL fakeShow(HWND w,int command) { ++shows; if(command==SW_SHOW)windows.at(w).visible=true;else if(command==SW_RESTORE)windows.at(w).iconic=false; return TRUE; }
static BOOL fakePos(HWND w,HWND after,int,int,int,int,UINT flags) {
    if(w!=g_hwnd){assert(after==HWND_TOP);assert(flags&SWP_ASYNCWINDOWPOS);assert(flags&SWP_NOACTIVATE);}return TRUE;
}
static BOOL fakeSetForeground(HWND w) {
    ++requests;destinations.push_back(w);requestTimes.push_back(clockMs);
    if(acceptForeground)foreground=w;return acceptForeground;
}
static BOOL fakeKill(HWND,UINT_PTR id) { timers.erase(id);return TRUE; }
static UINT_PTR fakeTimer(HWND,UINT_PTR id,UINT interval,TIMERPROC) { assert(interval>0);if(!timerWorks)return 0;timers.insert(id);return id; }
static BOOL fakePost(HWND,UINT msg,WPARAM w,LPARAM l) { assert(msg==WM_TIMER&&l==0);postedTimers.push_back(w);return TRUE; }
#define OpenProcess fakeOpen
#define WaitForSingleObject fakeWait
#define CloseHandle fakeClose
#define QueryFullProcessImageNameW fakeQuery
#define GetProcessTimes fakeTimes
#define IsWindow fakeIsWindow
#define IsWindowVisible fakeVisible
#define IsIconic fakeIconic
#define GetWindowThreadProcessId fakeThread
#define GetAncestor fakeAncestor
#define GetWindowLongPtrW fakeStyle
#define DwmGetWindowAttribute fakeDwm
#define GetWindowRect fakeRect
#define MonitorFromWindow fakeMonitor
#define GetForegroundWindow fakeForeground
#define GetCurrentProcessId fakeCurrentPid
#define GetTickCount64 fakeTick
#define ShowWindowAsync fakeShow
#define SetWindowPos fakePos
#define SetForegroundWindow fakeSetForeground
#define KillTimer fakeKill
#define SetTimer fakeTimer
#define PostMessageW fakePost
#include "resume_focus.h" // Actual foreground request implementation, Fake OS.
static std::wstring U2W(const std::string& text) { return std::wstring(text.begin(),text.end()); }
static bool focusQueryProcessIdentity(DWORD pid,std::wstring* path,ULONGLONG* birth) {
    if(!processes.count(pid)||!processes.at(pid).access)return false;
    if(path)*path=processes.at(pid).path;if(birth)*birth=processes.at(pid).birth;return true;
}
static std::wstring focusWindowClass(HWND w) { return fakeIsWindow(w)?windows.at(w).cls:L""; }
static bool focusMonitorInfo(HMONITOR,std::wstring* device,RECT* rect) { if(device)*device=L"Display";if(rect)*rect={0,0,1920,1080};return true; }
static bool focusWindowLooksFullscreen(HWND,RECT) { return true; }
static bool focusGameControlBlocked() { return blocked||g_manualPausedPid.load()!=0; }
static bool focusPidIsManuallyPaused(DWORD pid) { return pid&&pid==g_manualPausedPid.load(); }
static bool focusMainWindowIsForeground() { return foreground==g_hwnd; }
static unsigned long long currentPowerGeneration() { return powerGeneration; }
static HWND focusFindWindowForPid(DWORD pid,const FocusTargetSnapshot* preferred=nullptr) {
    ++findWindows;if(preferred)assert(preferred->pid==pid);
    if(onFind){auto fn=std::move(onFind);onFind={};fn();}
    for(const auto& [w,win]:windows)if(win.pid==pid&&win.visible&&!win.tool&&!win.cloaked)return w;
    return nullptr;
}
static DWORD focusFindProcessByPath(const std::wstring&) { ++findProcesses;return 0; }
static Game nativeDetectGame() { ++detectCalls;return {}; }
static bool focusResolveTarget(FocusTargetSnapshot&);
static FocusTargetSnapshot focusRememberedGameTarget() { auto t=g_rememberedGameTarget;if(t.valid&&focusResolveTarget(t))return t;return {}; }
static void hideWindowAnimated(HWND w) { windows.at(w).visible=false;if(foreground==w)foreground=afterHide; }
static void focusLosslessReturnCancel();
static void focusLosslessReturnInvalidate();
using IpcFn=std::function<json(const json&)>;
static std::map<std::string,IpcFn> commands;
static void ipc_on(const std::string& cmd,IpcFn fn) { commands[cmd]=std::move(fn); }
#include "production_focus.inc"
static int passed=0;
static void check(bool condition,const char* label) {
    if(!condition){std::cerr<<"FAIL "<<label<<"\n";std::exit(1);}++passed;std::cout<<"PASS "<<label<<"\n";
}
static FocusTargetSnapshot target(DWORD pid=42,HWND w=h(2)) {
    FocusTargetSnapshot t;t.pid=pid;t.hwnd=w;t.processCreated=processes.at(pid).birth;t.path=processes.at(pid).path;t.valid=true;t.fullscreen=true;return t;
}
static json identity(bool enabled=true,DWORD pid=42) { const auto t=target(pid);return {{"enabled",enabled},{"pid",pid},{"processCreated",std::to_string(t.processCreated)},{"path",std::string(t.path.begin(),t.path.end())}}; }
static json call(const json& args) { return commands.at("game.losslessReturnTarget")(args); }
static void reset() {
    g_losslessReturnFocus={};g_losslessReturnArmed=false;g_focusSession={};g_pendingSummonTarget={};timers.clear();postedTimers.clear();
    windows={{h(1),{1}},{h(2),{42}},{h(3),{77}},{h(4),{55}},{h(5),{43}}};
    windows.at(h(4)).cls=L"Notepad";
    processes={{1,{L"C:\\YMCC.exe"}},{42,{L"C:\\Games\\Game.exe"}},{77,{L"C:\\LS\\LosslessScaling.exe"}},{55,{L"C:\\Windows\\notepad.exe"}},{43,{L"C:\\Games\\Other.exe",456}}};
    foreground=h(1);afterHide=h(3);clockMs=10000;powerGeneration=1;g_losslessReturnIdentityEpoch=1;g_exitRequested=false;g_powerLifecycle=PowerLifecycle::Ready;g_manualPausedPid=0;
    g_rememberedGameTarget=target();g_rememberedGameDeadline=clockMs+60000;g_gameValveTarget.game={42,123,processes.at(42).path};
    g_focusSession.target=target(77,h(3));g_focusSession.active=true;g_focusSession.ownedTopmost=true;
    requests=findWindows=findProcesses=detectCalls=opens=closes=shows=0;destinations.clear();requestTimes.clear();acceptForeground=timerWorks=true;blocked=false;onFind={};onQuery={};
}
static void arm() { if(call(identity())["ok"]!=true){std::cerr<<"arm rejected\n";std::exit(1);} }
static void begin() { focusBeginReturnToPreviousWindow(clockMs); }
static void tick(ULONGLONG relative) { clockMs=10000+relative;dispatchReturnTimer(); }
static void noResources(const char* label) { check(timers.empty()&&!g_losslessReturnFocus.target.valid&&!g_losslessReturnArmed&&!g_focusSession.returning,label); }
static void noMoreRequests() { const int before=requests;tick(11000);tick(20000);check(requests==before,"late timers cannot revive a stopped session"); }
int main() {
    registerLosslessIpc();
    for(int mode=0;mode<=3;++mode){g_asyncMode=mode;check(!ipc_cmd_async("game.losslessReturnTarget"),"IPC executes on owner/UI thread in every async mode");}
    reset();auto args=identity();args.erase("enabled");check(call(args)["ok"]==true&&timers.empty()&&requests==0,"registration defaults ON with no immediate focus or polling");
    begin();check(requests==1&&destinations.back()==h(2)&&g_focusSession.target.pid==42,"double-B overrides LS snapshot with the exact identified game");
    tick(1000);tick(3000);tick(9999);check(requests==1&&g_losslessReturnFocus.returning,"already foreground game consumes no additional request budget");tick(10000);noResources("10s deadline destroys timer and target even after success");noMoreRequests();

    reset();arm();acceptForeground=false;begin();tick(999);check(requests==1,"no retry before 1s");tick(1000);tick(2999);check(requests==2,"second actual request at 1s; none before 3s");tick(3000);for(int i=3100;i<10000;i+=100)tick(i);
    check(requestTimes==std::vector<ULONGLONG>({10000,11000,13000})&&destinations==std::vector<HWND>({h(2),h(2),h(2)}),"denied foreground requests spend exactly three attempts at 0/1/3s");tick(10000);noResources("failed activation does not leave permanent polling");noMoreRequests();

    reset();arm();foreground=h(2);windows.at(h(2)).visible=false;windows.at(h(2)).iconic=true;begin();check(requests==0&&shows==0,"already-game check precedes all show/restore/foreground operations");foreground=h(3);tick(6000);check(requests==1&&foreground==h(2),"LS steal at 6s uses budget retained after already-game success");foreground=h(3);tick(6100);check(requests==1,"late correction retains minimum retry spacing");tick(7000);foreground=h(3);tick(8000);tick(9000);check(requests==3,"repeated late LS steals never exceed three actual requests");tick(10000);noResources("late corrections cannot extend the absolute 10s deadline");

    reset();arm();begin();foreground=h(4);tick(100);check(requests==1,"user application switch cancels immediately without 900ms grace");noResources("user switch clears all owned return resources");foreground=h(3);noMoreRequests();
    reset();arm();afterHide=h(4);begin();check(requests==0,"user application already foreground after hide is respected");noResources("initial user switch has no retry timer");

    for(int kind=0;kind<8;++kind){reset();auto bad=identity();if(kind==0)bad["pid"]=43;if(kind==1)bad["processCreated"]="456";if(kind==2)bad["path"]="C:\\Games\\Other.exe";if(kind==3)g_rememberedGameTarget={};if(kind==4)g_rememberedGameDeadline=9999;if(kind==5)g_rememberedGameTarget.pid=43;if(kind==6)g_rememberedGameTarget.processCreated=456;if(kind==7)g_rememberedGameTarget.path=L"C:\\Other.exe";
      check(call(bad)["ok"]==false&&!g_losslessReturnFocus.target.valid&&requests==0&&findProcesses==0&&detectCalls==0,"registration rejects missing/stale/different identified game without rescanning");}
    for(const auto& birth:std::vector<json>{0,-1,123.0,"+123","123x","18446744073709551616",""}){reset();auto bad=identity();bad["processCreated"]=birth;check(call(bad)["ok"]==false,"invalid, floating or overflowing birth is rejected losslessly");}
    reset();processes.at(42).birth=134123456789012345ULL;g_rememberedGameTarget=target();g_gameValveTarget.game.processCreated=processes.at(42).birth;check(call(identity())["ok"]==true,"FILETIME string retains all 64-bit digits");
    reset();auto casePath=identity();casePath["path"]="c:\\games\\GAME.EXE";check(call(casePath)["ok"]==true,"Windows path identity comparison is case insensitive");

    for(int kind=0;kind<6;++kind){reset();if(kind==0)processes.at(42).access=false;if(kind==1)processes.at(42).alive=false;if(kind==2)processes.at(42).query=false;if(kind==3)processes.at(42).times=false;if(kind==4)processes.at(42).birth=456;if(kind==5)processes.at(42).path=L"C:\\Wrong.exe";check(call(identity())["ok"]==false,"registration fails closed on live process identity/lifetime failures");}
    for(int kind=0;kind<8;++kind){reset();arm();if(kind==0)processes.at(42).alive=false;if(kind==1)processes.at(42).birth=456;if(kind==2)processes.at(42).path=L"C:\\Wrong.exe";if(kind==3)processes.at(42).access=false;if(kind==4)++powerGeneration;if(kind==5)g_powerLifecycle=PowerLifecycle::Suspended;if(kind==6)g_exitRequested=true;if(kind==7)g_manualPausedPid=42;begin();check(requests==0&&findProcesses==0&&detectCalls==0,"return revalidates PID/birth/path, power, exit and paused gates without fallback");noResources("stale return removes target and timer");noMoreRequests();}
    reset();arm();begin();processes.at(42).alive=false;tick(1000);noResources("game exit during retry cancels session");processes.at(42).alive=true;processes.at(42).birth=456;noMoreRequests();

    reset();arm();windows.erase(h(2));windows.emplace(h(6),Window{42});begin();check(requests==1&&destinations.back()==h(6)&&findWindows==1&&findProcesses==0,"same live PID may reacquire a recreated game window");
    reset();arm();windows.erase(h(2));windows.emplace(h(6),Window{43});begin();tick(1000);check(requests==0&&findProcesses==0,"window replacement may not bind another game/process");tick(10000);noResources("missing game window wait remains bounded");
    reset();arm();windows.erase(h(2));windows.emplace(h(6),Window{42});onFind=[] {processes.at(42).birth=456;};begin();check(requests==0,"PID reuse during HWND resolution is caught by final identity validation");noResources("identity race clears the session");
    reset();arm();windows.erase(h(2));windows.emplace(h(6),Window{42});onFind=[] {foreground=h(4);};begin();check(requests==0,"user switch during HWND resolution is respected by final foreground gate");
    reset();arm();windows.erase(h(2));windows.emplace(h(6),Window{42});onFind=[] {++powerGeneration;};begin();check(requests==0,"new power generation during resolution cannot activate");
    reset();arm();onQuery=[] {onQuery=[] {windows.at(h(2)).pid=43;};};begin();check(requests==0,"HWND PID change during final identity query cannot activate another game");noResources("changed HWND owner clears the session");
    reset();arm();onQuery=[] {clockMs=21000;};begin();check(requests==0,"query crossing the 10s deadline cannot issue a late foreground request");noResources("deadline crossing during OS query destroys resources");

    reset();arm();begin();auto wrong=identity(false,43);check(call(wrong)["ok"]==false&&g_losslessReturnFocus.returning,"old game's OFF cannot clear another game's session");check(call(identity(false))["ok"]==true,"matching OFF clears active session");noResources("OFF destroys the owned timer");check(call(identity(false))["ok"]==true,"repeated OFF is idempotent when empty");noMoreRequests();
    reset();arm();check(call(identity(false))["ok"]==true,"matching OFF clears armed target before return");noResources("armed state has no resource left after OFF");
    reset();arm();auto wrongOn=identity();wrongOn["processCreated"]="456";check(call(wrongOn)["ok"]==false&&g_losslessReturnFocus.target.pid==42,"invalid ON does not destroy an existing valid target");

    reset();arm();begin();foreground=h(3);g_pendingSummonTarget=target(77,h(3));focusPrepareSummonSession();noResources("new summon cancels old bounded return state");noMoreRequests();
    reset();arm();begin();focusClearSession();focusClearSession();noResources("common clear is idempotent and destroys resources");noMoreRequests();
    reset();arm();begin();focusLosslessReturnInvalidate();check(!postedTimers.empty(),"game selection changes post an owner-thread invalidation");while(!postedTimers.empty()){postedTimers.pop_front();dispatchReturnTimer();}noResources("identity invalidation destroys active timer on owner thread");noMoreRequests();
    reset();arm();focusLosslessReturnInvalidate();dispatchReturnTimer();noResources("identity invalidation clears a merely armed target without starting a timer");
    for(auto phase:{PowerLifecycle::Suspending,PowerLifecycle::Suspended,PowerLifecycle::Resuming}){reset();arm();begin();g_powerLifecycle=phase;dispatchReturnTimer();noResources("non-ready power phase cancels bounded return");g_powerLifecycle=PowerLifecycle::Ready;noMoreRequests();}
    for(WPARAM event:{WPARAM(PBT_APMSUSPEND),WPARAM(PBT_APMRESUMEAUTOMATIC),WPARAM(PBT_APMRESUMESUSPEND),WPARAM(PBT_APMRESUMECRITICAL)}){reset();arm();begin();dispatchPowerCancel(event);noResources("actual WM_POWERBROADCAST cancellation clears target and timer");noMoreRequests();}
    reset();arm();begin();g_exitRequested=true;focusLosslessReturnCancel();noResources("exit cancellation destroys owned timer before async cleanup");noMoreRequests();
    reset();arm();timerWorks=false;begin();noResources("timer creation failure terminates session synchronously");noMoreRequests();
    reset();arm();acceptForeground=false;begin();clockMs=10900;begin();tick(1000);tick(3000);check(requests==3&&g_losslessReturnFocus.started==10000,"duplicate return entry cannot reset budget or deadline");tick(10000);noResources("duplicate B preserves original deadline");
    reset();arm();begin();focusClearSession();foreground=h(1);g_focusSession.target=target(77,h(3));arm();dispatchReturnTimer();check(!g_losslessReturnFocus.returning&&requests==1&&timers.empty(),"old queued timer cannot start a newly armed session");
    reset();arm();acceptForeground=false;begin();clockMs=1000000;dispatchReturnTimer();check(requests==1,"delayed timer cannot make catch-up requests after deadline");noResources("late timer destroys resources at first delivered tick");
    reset();arm();begin();check(opens==closes,"all identity process handles are closed on success");
    reset();g_rememberedGameTarget={};focusLosslessRememberDetectedGame(42,123,processes.at(42).path,target());check(g_rememberedGameTarget.pid==42&&g_rememberedGameDeadline==clockMs+21600000,"worker publishes the current game snapshot without a focus operation");
    for(int kind=0;kind<4;++kind){reset();auto old=target();g_rememberedGameTarget={};if(kind==0)g_gameValveTarget.game.pid=43;if(kind==1)g_gameValveTarget.game.processCreated=456;if(kind==2)g_gameValveTarget.game.path=L"C:\\Other.exe";if(kind==3)old.processCreated=456;focusLosslessRememberDetectedGame(42,123,processes.at(42).path,old);check(!g_rememberedGameTarget.valid,"late detection publication cannot restore a replaced identity");}
    reset();{std::lock_guard<std::mutex> enumerationLock(g_gameValveMx);check(call(identity())["ok"]==true,"UI registration never waits for the game enumeration mutex");}
    reset();g_focusSession.returning=true;g_focusSession.returnStarted=clockMs;timers.insert(RETURN_GAME_FOCUS_TIMER_ID);arm();check(timers.empty()&&!g_focusSession.returning&&!g_losslessReturnFocus.returning,"late registration stops an older ordinary return timer without arming a permanent poll");dispatchReturnTimer();check(requests==0&&timers.empty(),"late ordinary timer cannot trigger focus on the new pending target");
    reset();focusLosslessReturnInvalidate();check(postedTimers.empty(),"game selection changes post no extra UI messages without a registered target");
    reset();arm();dispatchPowerCancel(PBT_APMSUSPEND);noResources("sleep clears an armed target even before its first return");
    reset();g_focusSession.target=target(55,h(4));begin();check(requests==1&&foreground==h(4),"ordinary return-to-application remains available without registered LS target");
    std::cout<<"lossless return production selftest: "<<passed<<" checks PASS\n";
}
