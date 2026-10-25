#include <iostream>
#include <vector>
#include <stdexcept>
#include <filesystem>
#include "lossless_launch.h"
#include "lossless_scale.h"
#include "lossless_runtime.h"
using namespace ymcc::lossless;
static unsigned checks = 0;
static void require(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
struct Fake {
    bool allowed = true, valid = true, locked = true, alive = true, present = false, denyStart = false, revokeAfterStart = false;
    int locks = 0, unlocks = 0, scans = 0, starts = 0, readyCalls = 0, revokeAt = 0;
    std::vector<ScanState> sequence;
    Identity identity{77, 123456789, L"C:\\Fixture\\LosslessScaling.exe"};
    bool ready() { ++readyCalls; return allowed && !(revokeAt && readyCalls >= revokeAt); }
    bool validate() { return valid; }
    bool lock() { ++locks; return locked; }
    void unlock() { ++unlocks; }
    unsigned error() { return 5; }
    Scan scan() { const auto index = scans++; const auto state = sequence.empty() ? (present ? ScanState::Exact : ScanState::Absent) : sequence[(std::min)(std::size_t(index), sequence.size()-1)]; return {state, identity, 5}; }
    bool matches(const Identity& value) { return alive && value.pid == identity.pid && value.created == identity.created && value.path == identity.path; }
    Result start() { ++starts; if (denyStart) return {false,false,false,{},"create-process-failed",5}; present=true; if(revokeAfterStart)allowed=false; return {true,true,false,identity,"",0}; }
};
// Deterministic ports exercise the production scale contract without UIA or apps.
struct FakeScalePorts {
    bool allowed=true, identity=true, locked=true, automation=true, foreground=true, accept=true, attempt=true;
    bool revokeOnObserve=false, replaceOnObserve=false;
    int locks=0,unlocks=0,observes=0,invokes=0,cancels=0,begins=0,stops=0;bool requestedReapply=false,stopAccept=true;
    int switchAfter=-1;
    bool requestedReset=false,resetClosed=true;int resets=0;
    bool resetOnFailure(){return requestedReset;}
    ScaleFailureReset resetFailedScaling(){require(locks==1&&unlocks==0,"failure reset must retain original action mutex");++resets;return {true,true,resetClosed,resetClosed,0,resetClosed?"failure-reset-closed":"failure-reset-terminate-denied"};}
    bool requestedRefresh=false;int refreshes=0;WindowRefreshResult refreshResult;
    bool refreshRequested(){return requestedRefresh;}
    WindowRefreshResult refreshOnce(){++refreshes;clock+=120;return refreshResult;}
    bool gameAlive=true;
    Identity game{110,4567,L"D:\\Games\\a.exe"},foregroundGame=game;
    std::uint64_t clock=0;
    std::vector<ScaleSnapshot> sequence;
    bool ready(){return allowed;}
    bool valid(){return identity&&gameAlive;}
    bool lock(){++locks;return locked;}
    void unlock(){++unlocks;}
    bool begin(){++begins;return automation;}
    std::uint64_t now(){return clock;}
    std::uint64_t windowBudget(){return 2500;}
    std::uint64_t totalBudget(){return 9000;}
    void pause(){clock+=75;}
    bool foregroundMatches(){return foreground&&scaleGameMatches(game,foregroundGame)&&(switchAfter<0||observes<=switchAfter);}
    ScaleSnapshot observe(){const auto index=observes++;if(revokeOnObserve)allowed=false;if(replaceOnObserve)identity=false;return sequence.empty()?ScaleSnapshot{}:sequence[(std::min)(std::size_t(index),sequence.size()-1)];}
    ScaleInvoke invokeOnce(){++invokes;return {attempt,accept};}
    bool reapply(){return requestedReapply;}
    ScaleInvoke stopOnce(){++stops;return {attempt,stopAccept};}
    bool cancelPending(){++cancels;return accept;}
};
static ScaleSnapshot snap(ScaleButton state,bool matches=true){return {true,state,matches,L"fixture",L"fixture"};}
struct FakeWindowRefreshPorts {
    std::uint64_t clock=0;std::uintptr_t foreground=1;std::uint32_t input=20;
    bool live=true,allowed=true,inputKnown=true,createOk=true,activateOk=true,returnOk=true,destroyOk=true;
    bool helperMoves=true,returnMoves=true,created=false;int creates=0,activations=0,returns=0,destroys=0;
    int inputAt=-1,switchAt=-1,revokeAt=-1,dieAt=-1;bool switchAfterReturn=false,throwAfterActivation=false;
    std::uintptr_t target(){return 1;}std::uintptr_t helper(){return 2;}
    std::uint64_t now(){return clock;}void pause(){clock+=20;}
    WindowRefreshState observe(){
        if(throwAfterActivation&&activations)throw std::runtime_error("fixture exception");
        if(inputAt>=0&&clock>=unsigned(inputAt))input=21;
        if(switchAt>=0&&clock>=unsigned(switchAt))foreground=3;
        if(revokeAt>=0&&clock>=unsigned(revokeAt))allowed=false;
        if(dieAt>=0&&clock>=unsigned(dieAt))live=false;
        return {live,allowed,inputKnown,foreground,input};
    }
    bool create(){++creates;created=createOk;return createOk;}
    bool activateHelper(){++activations;if(helperMoves&&activateOk)foreground=2;return activateOk;}
    bool activateGame(){++returns;if(switchAfterReturn)foreground=3;else if(returnMoves)foreground=1;return returnOk;}
    bool destroy(){++destroys;if(destroyOk){created=false;if(foreground==2)foreground=1;}return destroyOk;}
};
static void refreshChecks(){
    {FakeWindowRefreshPorts p;const auto r=refreshGameWindowOnce(p);require(r.refreshed&&r.leftGame&&r.returned&&r.helperDestroyed&&r.elapsedMs==120,"one real activation round trip is observed and helper destroyed");require(p.creates==1&&p.activations==1&&p.returns==1&&p.destroys==1&&!p.created,"one helper/away/back/cleanup, no surviving work");}
    {FakeWindowRefreshPorts p;p.activateOk=false;const auto r=refreshGameWindowOnce(p);require(!r.refreshed&&r.reason=="window-refresh-activation-denied"&&p.returns==0&&p.destroys==1,"denied foreground never falls back or retries");}
    {FakeWindowRefreshPorts p;p.helperMoves=false;const auto r=refreshGameWindowOnce(p);require(!r.refreshed&&r.elapsedMs==300&&p.activations==1&&p.returns==0&&!p.created,"accepted API without real foreground transition times out honestly");}
    {FakeWindowRefreshPorts p;p.returnMoves=false;const auto r=refreshGameWindowOnce(p);require(!r.refreshed&&r.elapsedMs==900&&p.returns==1&&p.destroys==1,"accepted return without real focus cannot confirm and never repeats");}
    {FakeWindowRefreshPorts p;p.returnOk=false;const auto r=refreshGameWindowOnce(p);require(r.refreshed&&p.returns==1,"observed foreground is authoritative over API return value");}
    {FakeWindowRefreshPorts p;p.inputAt=40;const auto r=refreshGameWindowOnce(p);require(r.cancelled&&!r.refreshed&&p.returns==0&&!p.created,"new input cancels without pulling game back");}
    {FakeWindowRefreshPorts p;p.switchAt=40;const auto r=refreshGameWindowOnce(p);require(r.cancelled&&p.returns==0&&p.foreground==3&&!p.created,"explicit application switch respected after helper activation");}
    {FakeWindowRefreshPorts p;p.revokeAt=40;const auto r=refreshGameWindowOnce(p);require(r.cancelled&&p.returns==0&&!p.created,"power/exit generation cancellation destroys helper without returning focus");}
    {FakeWindowRefreshPorts p;p.dieAt=40;const auto r=refreshGameWindowOnce(p);require(r.cancelled&&p.returns==0&&!p.created,"game exit or identity change never rebinds to another process");}
    {FakeWindowRefreshPorts p;p.switchAfterReturn=true;const auto r=refreshGameWindowOnce(p);require(r.cancelled&&p.returns==1&&p.foreground==3&&!p.created,"late foreign foreground never reclaimed");}
    {FakeWindowRefreshPorts p;p.destroyOk=false;const auto r=refreshGameWindowOnce(p);require(!r.refreshed&&r.reason=="window-refresh-cleanup-unconfirmed","failed helper cleanup is never accepted");}
    {FakeWindowRefreshPorts p;p.foreground=3;const auto r=refreshGameWindowOnce(p);require(!r.attempted&&p.creates==0&&p.returns==0,"foreign initial foreground cannot create or activate helper");}
    {FakeWindowRefreshPorts p;p.inputKnown=false;require(!refreshGameWindowOnce(p).refreshed&&p.creates==0,"unknown input admission never changes focus");}
    {FakeWindowRefreshPorts p;p.allowed=false;require(!refreshGameWindowOnce(p).refreshed&&p.creates==0,"unready context never creates a helper");}
    {FakeWindowRefreshPorts p;p.createOk=false;const auto r=refreshGameWindowOnce(p);require(!r.refreshed&&p.activations==0&&p.destroys==0,"failed helper creation has no foreground side effect");}
    {FakeWindowRefreshPorts p;p.throwAfterActivation=true;bool threw=false;try{refreshGameWindowOnce(p);}catch(const std::runtime_error&){threw=true;}require(threw&&p.destroys==1&&!p.created,"exception unwinding destroys the transient helper");}
    // Real Win32 construction/teardown, with no activation request. Target and
    // helper belong exclusively to this fixture; no game or product is started.
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC.Lossless.RefreshFixture";
    const auto atom=RegisterClassW(&wc);require(atom!=0,"fixture target class registered");
    const auto window=CreateWindowW(MAKEINTRESOURCEW(atom),L"",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    require(window!=nullptr,"hidden self-owned target created");
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);FILETIME birth{},exit{},kernel{},user{};
    require(GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user)!=FALSE,"real fixture identity available");
    const Identity identity{GetCurrentProcessId(),(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime,path};
    {GameWindowRefreshPorts ports(window,identity,[]{return true;});require(ports.create(),"actual native alpha helper created");
     const auto helper=reinterpret_cast<HWND>(ports.helper());const auto style=GetWindowLongPtrW(helper,GWL_EXSTYLE);
     require((style&WS_EX_TOOLWINDOW)&&(style&WS_EX_LAYERED)&&!(style&WS_EX_NOACTIVATE),"native helper has no taskbar entry but permits real activation");
     BYTE alpha=0;DWORD flags=0;require(GetLayeredWindowAttributes(helper,nullptr,&alpha,&flags)&&alpha==1&&(flags&LWA_ALPHA),"actual helper nearly transparent");
     RECT rect{};require(GetWindowRect(helper,&rect)&&rect.right-rect.left==1&&rect.bottom-rect.top==1,"actual helper is one pixel");
     require(ports.destroy()&&!IsWindow(helper),"actual Win32 helper destroyed before action returns");}
    require(DestroyWindow(window)!=FALSE&&UnregisterClassW(MAKEINTRESOURCEW(atom),wc.hInstance)!=FALSE,"fixture-owned target and class destroyed");
}
struct FakeRedrawPorts {
    bool live=true,allowed=true,accepted=true,switchAfter=false,revokeAfter=false,dieAfter=false;
    std::uintptr_t foreground=1;int requests=0;
    std::uint64_t now(){return requests;}
    std::uintptr_t target(){return 1;}
    WindowRefreshState observe(){return {live,allowed,false,foreground,static_cast<std::uint32_t>(requests)};}
    bool redraw(){++requests;if(switchAfter)foreground=3;if(revokeAfter)allowed=false;if(dieAfter)live=false;return accepted;}
};
static void redrawChecks(){
    {FakeRedrawPorts p;const auto r=redrawGameWindowOnce(p);require(r.attempted&&r.apiAccepted&&r.refreshed&&p.requests==1&&r.method==1&&r.methodName=="redraw"&&r.reason=="redraw-request-accepted","normal redraw submits once; accepted request is explicitly labelled, no input-idle requirement");require(!r.helperCreated&&!r.helperDestroyed&&!r.leftGame&&!r.returnRequested&&!r.returned,"normal redraw never creates helper or requests focus");}
    {FakeRedrawPorts p;p.accepted=false;const auto r=redrawGameWindowOnce(p);require(r.attempted&&!r.apiAccepted&&!r.refreshed&&p.requests==1&&r.reason=="window-redraw-request-denied","denied redraw has no retry or helper fallback");}
    {FakeRedrawPorts p;p.foreground=3;const auto r=redrawGameWindowOnce(p);require(!r.attempted&&p.requests==0&&r.reason=="window-refresh-target-changed","foreign foreground cannot be redrawn");}
    {FakeRedrawPorts p;p.live=false;require(!redrawGameWindowOnce(p).attempted&&p.requests==0,"invalid game identity rejects redraw");}
    {FakeRedrawPorts p;p.allowed=false;require(!redrawGameWindowOnce(p).attempted&&p.requests==0,"revoked power generation rejects redraw");}
    {FakeRedrawPorts p;p.switchAfter=true;const auto r=redrawGameWindowOnce(p);require(r.apiAccepted&&r.cancelled&&r.leftGame&&!r.refreshed&&p.foreground==3&&p.requests==1&&!r.returnRequested,"late app switch remains respected without foreground reclaim");}
    {FakeRedrawPorts p;p.revokeAfter=true;const auto r=redrawGameWindowOnce(p);require(r.apiAccepted&&r.cancelled&&!r.refreshed&&p.requests==1,"accepted redraw is not completion after context change");}
    {FakeRedrawPorts p;p.dieAfter=true;const auto r=redrawGameWindowOnce(p);require(r.cancelled&&!r.refreshed&&p.requests==1,"late game exit cannot confirm request");}
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC.Lossless.RedrawFixture";
    const auto atom=RegisterClassW(&wc);require(atom!=0,"redraw fixture class registered");
    const auto window=CreateWindowW(MAKEINTRESOURCEW(atom),L"",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);
    require(window!=nullptr,"redraw fixture owns hidden target");
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);FILETIME birth{},exit{},kernel{},user{};
    require(GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user)!=FALSE,"redraw fixture obtains real birth identity");
    const Identity identity{GetCurrentProcessId(),(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime,path};
    const auto foreground=GetForegroundWindow();RECT before{},after{};GetWindowRect(window,&before);
    {GameWindowRedrawPorts ports(window,identity,[]{return true;});require(ports.observe().valid,"real redraw adapter checks exact window owner and process identity");require(ports.redraw(),"actual RedrawWindow accepts self-owned target");}
    require(GetForegroundWindow()==foreground&&GetWindowRect(window,&after)&&EqualRect(&before,&after)&&!IsWindowVisible(window),"actual redraw leaves foreground, visibility and bounds unchanged");
    require(GameWindowRedrawPorts::flags==(RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN)&&!(GameWindowRedrawPorts::flags&(RDW_UPDATENOW|RDW_ERASENOW)),"real flags exclude synchronous paint dispatch");
    {auto wrong=identity;++wrong.created;GameWindowRedrawPorts ports(window,wrong,[]{return true;});require(!ports.observe().valid,"native adapter rejects reused PID with different birth");}
    {GameWindowRedrawPorts ports(window,identity,[]{return false;});require(!ports.observe().ready,"native adapter observes revoked context");}
    require(DestroyWindow(window)&&UnregisterClassW(MAKEINTRESOURCEW(atom),wc.hInstance),"redraw fixture resources destroyed");
}
struct FakeFramePorts:FakeRedrawPorts {
    bool frameOk=true,switchDuringFrame=false,revokeDuringFrame=false,dieDuringFrame=false;int frames=0;
    std::uint64_t now(){return frames+requests;}
    bool frameChanged(){++frames;if(switchDuringFrame)foreground=3;if(revokeDuringFrame)allowed=false;if(dieDuringFrame)live=false;return frameOk;}
    bool redraw(){require(frames==1&&frameOk,"redraw strictly follows one accepted frame request");return FakeRedrawPorts::redraw();}
};
static void frameChecks(){
    {FakeFramePorts p;const auto r=refreshGameFrameOnce(p);require(r.method==2&&r.methodName=="frame-changed"&&r.frameAccepted&&r.redrawAccepted&&r.apiAccepted&&r.refreshed&&r.reason=="frame-request-accepted"&&p.frames==1&&p.requests==1,"normal method2 submits one frame request then one redraw");require(!r.helperCreated&&!r.helperDestroyed&&!r.leftGame&&!r.returnRequested&&!r.returned,"method2 has no helper or focus request");}
    {FakeFramePorts p;p.frameOk=false;const auto r=refreshGameFrameOnce(p);require(r.attempted&&!r.frameAccepted&&!r.redrawAccepted&&!r.apiAccepted&&!r.refreshed&&r.reason=="window-frame-request-denied"&&p.frames==1&&p.requests==0,"failed frame request cannot fall back to method1 or retry");}
    {FakeFramePorts p;p.accepted=false;const auto r=refreshGameFrameOnce(p);require(r.frameAccepted&&!r.redrawAccepted&&!r.refreshed&&r.reason=="window-redraw-request-denied"&&p.frames==1&&p.requests==1,"accepted frame cannot mask a rejected redraw");}
    {FakeFramePorts p;p.foreground=3;require(!refreshGameFrameOnce(p).attempted&&p.frames==0&&p.requests==0,"foreign initial foreground prevents both frame and redraw");}
    {FakeFramePorts p;p.live=false;require(!refreshGameFrameOnce(p).attempted&&p.frames==0&&p.requests==0,"invalid initial identity prevents frame request");}
    {FakeFramePorts p;p.allowed=false;require(!refreshGameFrameOnce(p).attempted&&p.frames==0&&p.requests==0,"revoked context prevents frame request");}
    for(unsigned scenario=0;scenario<3;++scenario){FakeFramePorts p;p.switchDuringFrame=scenario==0;p.revokeDuringFrame=scenario==1;p.dieDuringFrame=scenario==2;const auto r=refreshGameFrameOnce(p);require(r.frameAccepted&&r.cancelled&&!r.apiAccepted&&!r.refreshed&&p.frames==1&&p.requests==0&&!r.returnRequested,"late foreground/identity/power change prevents second request without reclaim");}
    {FakeFramePorts p;p.switchAfter=true;const auto r=refreshGameFrameOnce(p);require(r.frameAccepted&&r.redrawAccepted&&r.apiAccepted&&r.cancelled&&!r.refreshed&&p.foreground==3&&!r.returnRequested,"late switch after accepted APIs is unconfirmed and never reclaimed");}
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC.Lossless.FrameFixture";
    const auto atom=RegisterClassW(&wc);require(atom!=0,"frame fixture class registered");
    const auto window=CreateWindowW(MAKEINTRESOURCEW(atom),L"",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);require(window!=nullptr,"frame fixture owns hidden target");
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);FILETIME birth{},exit{},kernel{},user{};require(GetProcessTimes(GetCurrentProcess(),&birth,&exit,&kernel,&user)!=FALSE,"frame fixture reads exact identity");
    const Identity identity{GetCurrentProcessId(),(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime,path};
    const auto foreground=GetForegroundWindow(),previous=GetWindow(window,GW_HWNDPREV);const auto style=GetWindowLongPtrW(window,GWL_STYLE);RECT before{},after{};GetWindowRect(window,&before);
    {GameWindowRedrawPorts ports(window,identity,[]{return true;});require(ports.observe().valid&&ports.frameChanged()&&ports.redraw(),"real Win32 adapter accepts SetWindowPos frame request then RedrawWindow");}
    require(GetForegroundWindow()==foreground&&GetWindow(window,GW_HWNDPREV)==previous&&GetWindowLongPtrW(window,GWL_STYLE)==style&&GetWindowRect(window,&after)&&EqualRect(&before,&after)&&!IsWindowVisible(window),"actual frame request preserves foreground, z-order, style, bounds and visibility");
    require(GameWindowRedrawPorts::frameFlags==(SWP_ASYNCWINDOWPOS|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED),"frame flags match original experiment method2 including async and no activation");
    require(DestroyWindow(window)&&UnregisterClassW(MAKEINTRESOURCEW(atom),wc.hInstance),"frame fixture resources destroyed");
}
struct FakeForegroundPorts {
    bool live=true,allowed=true,known=true,topOk=true,foregroundOk=true;
    bool switchAtTop=false,inputAtTop=false,dieAtTop=false,revokeAtTop=false,switchAtForeground=false,inputAtForeground=false;
    std::uintptr_t current=1;std::uint32_t input=20;unsigned tops=0,foregrounds=0;
    std::uintptr_t target(){return 1;}std::uint64_t now(){return tops+foregrounds;}
    WindowRefreshState observe(){return {live,allowed,known,current,input};}
    bool bringToTop(){++tops;if(switchAtTop)current=3;if(inputAtTop)++input;if(dieAtTop)live=false;if(revokeAtTop)allowed=false;return topOk;}
    bool requestForeground(){require(tops==1,"foreground strictly follows one top request");++foregrounds;if(switchAtForeground)current=3;if(inputAtForeground)++input;return foregroundOk;}
};
static void foregroundChecks(){
    {FakeForegroundPorts p;const auto r=requestGameForegroundOnce(p);require(r.method==3&&r.methodName=="foreground-request"&&r.topAccepted&&r.foregroundAccepted&&r.apiAccepted&&r.returnRequested&&r.returned&&r.refreshed&&r.reason=="foreground-request-accepted"&&p.tops==1&&p.foregrounds==1,"method3 requests BringWindowToTop and SetForegroundWindow exactly once");require(!r.frameAccepted&&!r.redrawAccepted&&!r.helperCreated&&!r.helperDestroyed&&!r.leftGame,"method3 does not mix frame/redraw/helper methods");}
    {FakeForegroundPorts p;p.topOk=false;const auto r=requestGameForegroundOnce(p);require(!r.topAccepted&&r.foregroundAccepted&&!r.apiAccepted&&!r.refreshed&&r.reason=="window-foreground-request-denied"&&p.tops==1&&p.foregrounds==1,"top API denial cannot be masked by foreground API acceptance");}
    {FakeForegroundPorts p;p.foregroundOk=false;const auto r=requestGameForegroundOnce(p);require(r.topAccepted&&!r.foregroundAccepted&&!r.apiAccepted&&!r.refreshed&&p.foregrounds==1,"foreground denial never retries or falls back to another method");}
    {FakeForegroundPorts p;p.current=3;const auto r=requestGameForegroundOnce(p);require(!r.attempted&&p.tops==0&&p.foregrounds==0,"initial foreign foreground is never reclaimed");}
    {FakeForegroundPorts p;p.known=false;require(!requestGameForegroundOnce(p).attempted&&p.tops==0,"unknown input cannot trigger an activation request");}
    {FakeForegroundPorts p;p.live=false;require(!requestGameForegroundOnce(p).attempted&&p.tops==0,"invalid initial identity prevents foreground request");}
    {FakeForegroundPorts p;p.allowed=false;require(!requestGameForegroundOnce(p).attempted&&p.tops==0,"unready power context prevents foreground request");}
    for(unsigned scenario=0;scenario<4;++scenario){FakeForegroundPorts p;p.switchAtTop=scenario==0;p.inputAtTop=scenario==1;p.dieAtTop=scenario==2;p.revokeAtTop=scenario==3;const auto r=requestGameForegroundOnce(p);require(r.cancelled&&!r.refreshed&&p.tops==1&&p.foregrounds==0,"new switch/input/identity/power change between APIs prevents SetForegroundWindow");}
    {FakeForegroundPorts p;p.switchAtForeground=true;const auto r=requestGameForegroundOnce(p);require(r.apiAccepted&&r.cancelled&&!r.refreshed&&r.leftGame&&!r.returned&&p.current==3&&p.foregrounds==1,"late app switch is respected with no second foreground request");}
    {FakeForegroundPorts p;p.inputAtForeground=true;const auto r=requestGameForegroundOnce(p);require(r.cancelled&&!r.refreshed&&p.foregrounds==1,"late new input cannot report confirmed activation");}
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC.Lossless.ForegroundFixture";
    const auto atom=RegisterClassW(&wc);require(atom!=0,"foreground fixture class registered");
    const auto window=CreateWindowW(MAKEINTRESOURCEW(atom),L"",WS_OVERLAPPEDWINDOW,0,0,64,64,nullptr,nullptr,wc.hInstance,nullptr);require(window!=nullptr,"foreground fixture owns hidden target");
    wchar_t path[32768]{};GetModuleFileNameW(nullptr,path,32768);FILETIME created{},exit{},kernel{},user{};require(GetProcessTimes(GetCurrentProcess(),&created,&exit,&kernel,&user)!=FALSE,"foreground fixture reads real process identity");
    const Identity identity{GetCurrentProcessId(),(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime,path};
    const auto before=GetForegroundWindow();unsigned step=0;
    {GameWindowRedrawPorts ports(window,identity,[]{return true;},[&](HWND h)->BOOL{require(h==window&&step++==0,"top boundary receives exact pinned HWND first");return TRUE;},[&](HWND h)->BOOL{require(h==window&&step++==1,"foreground boundary receives exact pinned HWND second");return FALSE;});
     require(ports.observe().valid&&ports.bringToTop()&&!ports.requestForeground()&&step==2,"real adapter propagates injected Win32 request receipts without targeting live apps");}
    require(GetForegroundWindow()==before&&!IsWindowVisible(window),"injected foreground fixture leaves real foreground untouched");
    require(DestroyWindow(window)&&UnregisterClassW(MAKEINTRESOURCEW(atom),wc.hInstance),"foreground fixture resources destroyed");
}
static void scaleChecks(){
    const Identity external{99,456,L"E:\\Portable\\LosslessScaling.exe"};
    {std::atomic<unsigned> discoveries{0},receipts{0};RuntimeMonitor monitor([&]{++discoveries;return selectRuntime({},false);});
     monitor.configure(false,false,[&](const RuntimeReceipt&){++receipts;});require(discoveries==0,"idle observer never enumerates");
     monitor.configure(true,false,[&](const RuntimeReceipt&){++receipts;});const auto deadline=GetTickCount64()+1000;
     while(receipts.load()==0&&GetTickCount64()<deadline)Sleep(5);
     monitor.stop();const auto count=discoveries.load();monitor.configure(true,true,{});require(count==1&&discoveries==count&&receipts==1,"native observer publishes once, joins and cannot restart after stop");}
    require(selectRuntime({},false).known&&!selectRuntime({},false).running,"absent runtime is known without launch");
    require(selectRuntime({external},false).identity.path==external.path,"unique external startup path accepted");
    require(!selectRuntime({external,external},false).known,"multiple instances never arbitrarily selected");
    require(!selectRuntime({external},true).known,"unreadable candidate never assumed absent");
    const auto off=snap(ScaleButton::Off),count=snap(ScaleButton::Countdown),on=snap(ScaleButton::On);
    {FakeScalePorts p;p.sequence={off,count,count,on};const auto r=ensureScaling(p);require(r.ok&&r.enabled==1&&r.invoked&&p.invokes==1&&p.unlocks==1,"one invoke through native countdown");}
    {FakeScalePorts p;p.sequence={on};for(int i=0;i<25;i++)require(ensureScaling(p).ok,"repeat already on");require(p.invokes==0&&p.unlocks==25,"already on never replays");}
    {FakeScalePorts p;p.sequence={count,count,on};require(ensureScaling(p).ok&&p.invokes==0,"existing countdown only observed");}
    {FakeScalePorts p;p.locked=false;const auto r=ensureScaling(p);require(r.pending&&p.invokes==0&&p.observes==0&&p.begins==0&&p.unlocks==0,"busy request never queues");}
    {FakeScalePorts p;p.allowed=false;require(!ensureScaling(p).ok&&p.locks==0&&p.invokes==0,"context unavailable before lock");}
    {FakeScalePorts p;p.identity=false;require(!ensureScaling(p).ok&&p.locks==0&&p.invokes==0,"identity rejects before lock");}
    {FakeScalePorts p;p.sequence={off};p.revokeOnObserve=true;require(!ensureScaling(p).ok&&p.invokes==0,"context revoked during observation");}
    {FakeScalePorts p;p.sequence={off};p.replaceOnObserve=true;require(!ensureScaling(p).ok&&p.invokes==0,"identity replaced during observation");}
    {FakeScalePorts p;p.automation=false;require(!ensureScaling(p).ok&&p.invokes==0&&p.observes==0&&p.unlocks==1,"no automatic focus fallback");}
    {FakeScalePorts p;const auto r=ensureScaling(p);require(r.reason=="scaling-state-unavailable"&&p.invokes==0&&p.clock>=2500,"unreadable state bounded and never toggled");}
    {FakeScalePorts p;p.sequence={snap(ScaleButton::Unknown)};require(!ensureScaling(p).ok&&p.invokes==0,"unknown button never toggled");}
    {FakeScalePorts p;p.sequence={snap(ScaleButton::On,false)};const auto r=ensureScaling(p);require(r.reason=="another-profile-active"&&p.invokes==0,"other active profile retained");}
    {FakeScalePorts p;p.sequence={off};p.foreground=false;require(ensureScaling(p).reason=="target-not-foreground"&&p.invokes==0&&p.cancels==0,"foreign foreground never activated");}
    {FakeScalePorts p;p.sequence={off,count};p.switchAfter=1;const auto r=ensureScaling(p);require(r.cancelled&&p.invokes==1&&p.cancels==1,"owned observable countdown best effort cancel once");}
    {FakeScalePorts p;p.sequence={count};p.foreground=false;ensureScaling(p);require(p.cancels==0&&p.invokes==0,"unowned countdown never cancelled");}
    {FakeScalePorts p;p.sequence={off,count};p.accept=false;const auto r=ensureScaling(p);require(r.pending&&r.invoked&&p.invokes==1,"ambiguous invoke never replayed");}
    {FakeScalePorts p;p.sequence={off};p.attempt=false;p.accept=false;const auto r=ensureScaling(p);require(!r.pending&&!r.invoked&&p.invokes==1,"pre-invoke race has no retry");}
    {FakeScalePorts p;p.sequence={off,count};const auto r=ensureScaling(p);require(r.pending&&p.clock>=9000&&p.invokes==1,"countdown timeout has no replay");}
    {FakeScalePorts p;p.sequence={off,off};const auto r=ensureScaling(p);require(!r.ok&&r.enabled==0&&p.invokes==1,"returned off does not toggle again");}
    require(scaleButtonState(L" 取消缩放 ")==ScaleButton::On&&scaleButtonState(L"無縮放")==ScaleButton::On,"localized on labels");
    require(scaleButtonState(L"缩放")==ScaleButton::Off&&scaleButtonState(L"Scale")==ScaleButton::Off,"localized off labels");
    require(scaleButtonState(L"5")==ScaleButton::Countdown&&scaleButtonState(L"unknown")==ScaleButton::Unknown,"countdown distinct from unknown");
    require(scaleProfileMatches(L"Scaling: window\nResolution: fixture\nProfile: \"Game\"",L"Game"),"actual third line profile");
    require(scaleProfileMatches(L"缩放：窗口\n大小：测试\n配置：‘Game’",L"Game")==false,"unsupported quotes remain unknown");
    require(!scaleProfileMatches(L"Profile: \"Game\"",L"Game")&&!scaleProfileMatches(L"a\nb\nProfile: \"Other\"",L"Game"),"selection or another active profile is not evidence");
    for(const auto game:{Identity{110,4567,L"D:\\Games\\a.exe"},Identity{220,8901,L"E:\\Other\\b.exe"},Identity{330,9101,L"F:\\Games\\third.exe"}}){
        FakeScalePorts p;p.game=game;p.foregroundGame=game;p.sequence={off,count,on};require(ensureScaling(p).ok&&p.invokes==1,"multiple YMCC game identities activate once");
        for(int mismatch=0;mismatch<3;mismatch++){
            FakeScalePorts wrong;wrong.game=game;wrong.foregroundGame=game;wrong.sequence={off};
            if(mismatch==0)++wrong.foregroundGame.pid;
            if(mismatch==1)++wrong.foregroundGame.created;
            if(mismatch==2)wrong.foregroundGame.path=L"C:\\Other\\same.exe";
            const auto r=ensureScaling(wrong);require(r.reason=="target-not-foreground"&&wrong.invokes==0,"wrong PID/reused PID/wrong image never toggles");
        }
    }
    {FakeScalePorts p;p.game.pid=0;p.game.created=0;p.sequence={off,count,on};require(ensureScaling(p).ok&&p.invokes==1,"PID absent permits foreground path-only admission");}
    {FakeScalePorts p;p.game.pid=0;p.game.created=0;p.foregroundGame.path=L"C:\\Apps\\Codex.exe";p.sequence={off};require(!ensureScaling(p).ok&&p.invokes==0,"path-only target still rejects Codex as another foreground");}
    {FakeScalePorts p;p.gameAlive=false;p.sequence={off};require(!ensureScaling(p).ok&&p.begins==0&&p.invokes==0,"exited game rejected before UIA");}
    {FakeScalePorts p;p.requestedReapply=true;p.sequence={on,off,count,on};const auto r=ensureScaling(p);require(r.ok&&r.restarted&&r.stopInvoked&&r.invoked&&p.stops==1&&p.invokes==1,"explicit same-profile click rebuilds exactly once");}
    {FakeScalePorts p;p.requestedReapply=true;p.sequence={off,count,on};const auto r=ensureScaling(p);require(r.ok&&!r.restarted&&p.stops==0&&p.invokes==1,"cold start does not reapply its own fresh activation");}
    {FakeScalePorts p;p.requestedReapply=true;p.sequence={count,count,on};require(ensureScaling(p).ok&&p.stops==0&&p.invokes==0,"existing countdown observed without stopping its new activation");}
    {FakeScalePorts p;p.requestedReapply=true;p.sequence={on,count,on};const auto r=ensureScaling(p);require(r.ok&&r.restarted&&p.stops==1&&p.invokes==0,"AutoScale owns a new countdown after explicit stop; no second start toggle");}
    {FakeScalePorts p;p.requestedReapply=true;p.sequence={on};const auto r=ensureScaling(p);require(!r.ok&&r.pending&&r.reason=="restart-stop-unconfirmed"&&p.stops==1&&p.invokes==0,"unchanged old On cannot falsely confirm stop/restart");}
    {FakeScalePorts p;p.requestedReapply=true;p.stopAccept=false;p.sequence={on};const auto r=ensureScaling(p);require(!r.ok&&p.stops==1&&p.invokes==0,"unconfirmed stop never followed by blind start");}
    {FakeScalePorts p;p.requestedReapply=true;p.sequence={snap(ScaleButton::On,false)};require(!ensureScaling(p).ok&&p.stops==0&&p.invokes==0,"reapply refuses another active profile");}
    {FakeScalePorts p;p.requestedReapply=true;p.foreground=false;p.sequence={on};require(!ensureScaling(p).ok&&p.stops==0&&p.invokes==0,"reapply does not change another foreground or stop before target admission");}
    {FakeScalePorts p;p.requestedReapply=true;p.sequence={on,off,count};p.switchAfter=2;const auto r=ensureScaling(p);require(!r.ok&&p.stops==1&&p.cancels==1,"game switch during reapply cancels only its own pending start");}
    {auto paused=on;paused.paused=true;FakeScalePorts p;p.sequence={paused};const auto r=ensureScaling(p);require(!r.ok&&r.pending&&r.enabled==-1&&r.reason=="scaling-paused"&&p.invokes==0,"paused On does not prove an active working session");}
    {auto paused=on;paused.paused=true;FakeScalePorts p;p.requestedReapply=true;p.sequence={paused,off,count,on};const auto r=ensureScaling(p);require(r.ok&&p.stops==1&&p.invokes==1,"explicit paused-session recovery is bounded to one rebuild");}
    {FakeScalePorts p;p.requestedRefresh=true;p.refreshResult.refreshed=true;p.sequence={off,count,on,on};const auto r=ensureScaling(p);require(r.ok&&r.refresh.refreshed&&p.refreshes==1&&p.observes==4,"explicit action refreshes once after On, then consumes a fresh receipt");}
    {auto paused=on;paused.paused=true;FakeScalePorts p;p.requestedRefresh=true;p.refreshResult.refreshed=true;p.sequence={off,paused,on};const auto r=ensureScaling(p);require(r.ok&&p.refreshes==1&&p.observes==3,"paused current-profile session can recover through one native refresh");}
    {FakeScalePorts p;p.requestedRefresh=true;p.refreshResult.reason="window-refresh-activation-denied";p.sequence={on};const auto r=ensureScaling(p);require(!r.ok&&r.pending&&r.reason=="window-refresh-activation-denied"&&p.refreshes==1,"failed refresh is not masked by pre-refresh On");}
    {FakeScalePorts p;p.requestedRefresh=true;p.sequence={snap(ScaleButton::On,false)};require(!ensureScaling(p).ok&&p.refreshes==0,"another active profile cannot trigger a window refresh");}
    {FakeScalePorts p;p.requestedRefresh=true;p.foreground=false;p.sequence={on};require(!ensureScaling(p).ok&&p.refreshes==0,"another foreground cannot trigger a refresh");}
    {FakeScalePorts p;p.sequence={on};require(ensureScaling(p).ok&&p.refreshes==0,"legacy/default request never refreshes windows");}
    {auto paused=on;paused.paused=true;FakeScalePorts p;p.requestedReset=true;p.sequence={off,paused};const auto r=ensureScaling(p);require(!r.ok&&r.reason=="scaling-paused"&&!r.failureReset.requested&&r.pending&&r.enabled==-1&&p.resets==0&&p.unlocks==1,"paused receipt preserves runtime and uncertainty even with legacy reset flag");}
    {FakeScalePorts p;p.requestedReset=true;const auto r=ensureScaling(p);require(r.reason=="scaling-state-unavailable"&&!r.failureReset.requested&&p.invokes==0&&p.resets==0,"unreadable returned state preserves the process and never resets");}
    {FakeScalePorts p;p.requestedReset=true;p.resetClosed=false;const auto r=ensureScaling(p);require(!r.ok&&!r.failureReset.requested&&!r.failureReset.closed&&p.resets==0,"ordinary reply cannot invoke terminate even when a reset hook exists");}
    {FakeScalePorts p;p.requestedReset=true;p.sequence={on};const auto r=ensureScaling(p);require(r.ok&&p.resets==0,"successful activation never resets the process");}
    {FakeScalePorts p;p.requestedReset=true;p.sequence={snap(ScaleButton::On,false)};const auto r=ensureScaling(p);require(r.reason=="another-profile-active"&&p.resets==0,"another profile cannot be killed by failure recovery");}
    {FakeScalePorts p;p.requestedReset=true;p.locked=false;ensureScaling(p);require(p.resets==0,"busy action cannot run cleanup");}
    {FakeScalePorts p;p.requestedReset=true;p.foreground=false;ensureScaling(p);require(p.invokes==0&&p.resets==0,"pre-action foreign foreground does not kill LS");}
    {FakeScalePorts p;p.requestedReset=true;p.automation=false;ensureScaling(p);require(p.resets==0,"unavailable automation is a returned state and cannot reset runtime");}
    {FakeScalePorts p;p.requestedReset=true;p.automation=false;p.foreground=false;ensureScaling(p);require(p.resets==0,"unavailable automation cannot reset an unrelated foreground session");}
    {FakeScalePorts p;p.requestedReset=true;p.revokeOnObserve=true;ensureScaling(p);require(p.resets==0,"changed power context never terminates a process");}
    {auto paused=on;paused.paused=true;FakeScalePorts p;p.requestedReset=true;p.requestedRefresh=true;p.refreshResult.refreshed=true;p.refreshResult.apiAccepted=true;p.refreshResult.frameAccepted=true;p.refreshResult.redrawAccepted=true;p.sequence={off,paused,paused};const auto r=ensureScaling(p);require(r.reason=="scaling-paused"&&r.receipt.readable&&r.refresh.refreshed&&r.invoked&&p.refreshes==1&&p.resets==0&&!r.failureReset.requested,"194431 replay: current-profile On plus accepted frame/redraw and pause hint must remain alive");}
    {FakeScalePorts p;p.requestedReset=true;p.switchAfter=1;p.sequence={off,count};const auto r=ensureScaling(p);require(r.reason=="target-not-foreground"&&r.invoked&&p.resets==0&&!r.failureReset.requested,"194431 retry replay: post-Invoke foreground loss cannot kill the process");}
    require(scalePaused(L"Scaling: game\nResolution: fixture\nProfile: Game\nForeground: Xbox"),"fourth status line marks paused");
    require(!scalePaused(L"Scaling: game\nResolution: fixture\nProfile: Game\n"),"trailing newline is not paused");
    std::wstring relative=L"LosslessScaling.exe";require(!scaleAbsolute(relative),"relative runtime path rejected");
}
static void failureResetNativeChecks(const std::filesystem::path& inert){
    wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);
    const auto directory=inert.parent_path().parent_path();std::filesystem::create_directories(directory);const auto executable=directory/L"LosslessScaling.exe";
    require(CopyFileW(module,executable.c_str(),FALSE)!=FALSE,"copy only fixture binary to exact LS basename for reset consumer");
    std::wstring command=L"\""+executable.wstring()+L"\" --lossless-reset-owned";STARTUPINFOW startup{sizeof(startup)};PROCESS_INFORMATION child{};
    require(CreateProcessW(executable.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,directory.c_str(),&startup,&child)!=FALSE,"start one disposable fixture child, never real LS");
    struct ChildCleanup{PROCESS_INFORMATION& p;~ChildCleanup(){if(WaitForSingleObject(p.hProcess,0)==WAIT_TIMEOUT)TerminateProcess(p.hProcess,2);CloseHandle(p.hThread);CloseHandle(p.hProcess);}} cleanup{child};
    FILETIME created{},exit{},kernel{},user{};require(GetProcessTimes(child.hProcess,&created,&exit,&kernel,&user)!=FALSE,"owned reset child birth available");
    const Identity identity{child.dwProcessId,(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime,executable.wstring()};
    {auto wrong=identity;++wrong.created;const auto r=terminateFailedLossless(wrong);require(!r.attempted&&!r.closed&&WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT,"wrong birth rejects termination and preserves child");}
    {auto wrong=identity;wrong.path=(directory/L"elsewhere"/L"LosslessScaling.exe").wstring();const auto r=terminateFailedLossless(wrong);require(!r.attempted&&!r.closed&&WaitForSingleObject(child.hProcess,0)==WAIT_TIMEOUT,"wrong full path rejects termination and preserves child");}
    {auto wrong=identity;wrong.pid=GetCurrentProcessId();require(!terminateFailedLossless(wrong).attempted,"current YMCC/fixture PID is never a reset target");}
    const auto foreground=GetForegroundWindow();const auto r=terminateFailedLossless(identity);
    require(r.requested&&r.attempted&&r.terminated&&r.closed&&WaitForSingleObject(child.hProcess,0)==WAIT_OBJECT_0,"real TerminateProcess and bounded exit receipt consume only owned child");
    require(GetForegroundWindow()==foreground,"native failure reset never activates a window");
}
int wmain(int argc, wchar_t** argv) {
    if(argc==2&&std::wstring(argv[1])==L"--lossless-reset-owned"){Sleep(10000);return 0;}
    try {
        refreshChecks();redrawChecks();frameChecks();foregroundChecks();scaleChecks();
        { Fake p; p.present=true; for(unsigned i=0;i!=500;++i){const auto r=ensureRunning(p);require(r.ok&&r.reused&&!r.started,"warm reuse");}require(p.starts==0&&p.locks==p.unlocks,"warm reuse never launches and unlocks"); }
        { Fake p; for(unsigned i=0;i!=500;++i)require(ensureRunning(p).ok,"cold then warm");require(p.starts==1&&p.locks==p.unlocks,"one launch across 500 actions"); }
        { Fake p; p.valid=false;require(!ensureRunning(p).ok&&p.starts==0&&p.locks==0,"invalid path"); }
        { Fake p; p.allowed=false;require(!ensureRunning(p).ok&&p.starts==0&&p.locks==0,"power/exit/deadline reject before lock"); }
        { Fake p; p.locked=false;require(!ensureRunning(p).ok&&p.starts==0&&p.unlocks==0,"nonblocking busy"); }
        for(const auto state:{ScanState::Unknown,ScanState::Conflict}){Fake p;p.sequence={state};require(!ensureRunning(p).ok&&p.starts==0&&p.unlocks==1,"unknown/foreign/duplicate instance rejected");}
        { Fake p;p.present=true;p.alive=false;require(!ensureRunning(p).ok&&p.starts==0,"changed PID/birth/path refuses replay"); }
        { Fake p;p.sequence={ScanState::Absent,ScanState::Exact};const auto r=ensureRunning(p);require(r.ok&&r.reused&&p.starts==0,"external arrival caught by second snapshot"); }
        { Fake p;p.sequence={ScanState::Absent,ScanState::Unknown};require(!ensureRunning(p).ok&&p.starts==0,"unknown second snapshot"); }
        { Fake p;p.revokeAt=4;require(!ensureRunning(p).ok&&p.starts==0,"context revoked just before create"); }
        { Fake p;p.denyStart=true;const auto r=ensureRunning(p);require(!r.ok&&r.error==5&&p.starts==1,"access denied has exactly one attempt"); }
        { Fake p;p.revokeAfterStart=true;const auto r=ensureRunning(p);require(!r.ok&&r.started&&!r.reused&&p.starts==1,"late power change reports started but unconfirmed"); }
        require(argc==2,"inert executable path required");
        failureResetNativeChecks(argv[1]);
        unsigned called=0;
        NativePorts ports(argv[1],[]{return true;},[&](LPCWSTR app,LPWSTR cmd,LPSECURITY_ATTRIBUTES,LPSECURITY_ATTRIBUTES,BOOL inherit,DWORD flags,LPVOID env,LPCWSTR directory,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION){
            ++called;require((startup->dwFlags&STARTF_USESHOWWINDOW)!=0&&startup->wShowWindow==SW_SHOWMINNOACTIVE,"real native startup flags must not activate");
            const std::wstring command=cmd;require(command==L"\""+std::wstring(app)+L"\" -StartMinimized","only fixed minimized argument, no toggle or game injection");
            require(!inherit&&env==nullptr&&directory&&flags==NORMAL_PRIORITY_CLASS,"child launch boundaries");SetLastError(ERROR_ACCESS_DENIED);return FALSE;
        });
        require(ports.validate(),"native validate existing inert executable");
        const auto denied=ports.start();require(!denied.ok&&denied.error==ERROR_ACCESS_DENIED&&called==1,"real Win32 adapter error receipt");
        std::cout<<"{\"pass\":true,\"checks\":"<<checks<<",\"repeatActions\":1000,\"realApplicationsStarted\":0}"<<std::endl;
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<std::endl;return 1; }
}
