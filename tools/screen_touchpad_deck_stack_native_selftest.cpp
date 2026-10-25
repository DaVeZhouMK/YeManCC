// Actual WinRT input hosts and two owned lower processes on a private desktop.
// Native input sink events and cursor observations are deterministic fixtures;
// no real input injection, device output, visible desktop or Steam operations.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static HWND fixtureForeground=nullptr;
static HDESK fixtureDesktop=nullptr;
static CURSORINFO fixtureCursor{sizeof(CURSORINFO),CURSOR_SUPPRESSED,nullptr,{200,180}};
static unsigned long long fixtureCursorQueries=0;
static HWND fixtureGetForeground(){return fixtureForeground;}
static BOOL fixtureGetCursorInfo(PCURSORINFO result){++fixtureCursorQueries;*result=fixtureCursor;return TRUE;}
#define GetForegroundWindow fixtureGetForeground
#define GetCursorInfo fixtureGetCursorInfo
#include <screen_touchpad_owner.h>
#undef GetCursorInfo
#undef GetForegroundWindow
#include <iostream>
#include <thread>
#include <cstdlib>
using namespace ymcc::screenpads;
static constexpr UINT promoteMessage=WM_APP+701;
static Json cases=Json::array();static bool passed=true;
static void check(const std::string& name,bool ok){passed=passed&&ok;cases.push_back({{"name",name},{"ok",ok}});}
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static LRESULT CALLBACK targetProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==promoteMessage){SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(h);return 1;}
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK ownerProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==kOwnerVisibilityMessage){consumeOwnerVisibility((UINT_PTR)w);return 0;}
    if(m==kOwnerRebindMessage){consumeOwnerRebind((UINT_PTR)w);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static HWND create(const wchar_t* name,WNDPROC proc,DWORD ex,DWORD style,HWND parent=nullptr){
    WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=name;c.lpfnWndProc=proc;
    if(!RegisterClassW(&c)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
    return CreateWindowExW(ex,name,L"Owned hidden desktop fixture",style,-8000,-8000,16000,16000,parent,nullptr,c.hInstance,nullptr);
}
struct Target{PROCESS_INFORMATION pi{};HWND window=nullptr;};
static bool message(HWND window,UINT m){DWORD_PTR r=0;return SendMessageTimeoutW(window,m,0,0,SMTO_BLOCK|SMTO_ABORTIFHUNG,3000,&r)!=0;}
static bool start(Target& t,const std::wstring& directory,const std::wstring& desktop,int index){
    wchar_t self[32768]{};if(!GetModuleFileNameW(nullptr,self,32768))return false;
    const std::wstring ready=directory+L"\\target-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(index)+L".txt";
    std::wstring command=L"\""+std::wstring(self)+L"\" --target \""+ready+L"\"",desk=desktop;
    STARTUPINFOW si{sizeof(si)};si.lpDesktop=desk.data();si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    if(!CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&t.pi))return false;
    CloseHandle(t.pi.hThread);t.pi.hThread=nullptr;
    for(int retry=0;retry<100&&!t.window;retry++){
        std::ifstream f{std::filesystem::path(ready)};unsigned long long hwnd=0;if(f>>hwnd)t.window=(HWND)(ULONG_PTR)hwnd;
        if(!t.window)Sleep(20);
    }
    DWORD pid=0;return t.window&&IsWindow(t.window)&&GetWindowThreadProcessId(t.window,&pid)&&pid==t.pi.dwProcessId&&pid!=GetCurrentProcessId();
}
static bool stop(Target& t){
    if(!t.pi.hProcess)return true;DWORD pid=0;if(t.window&&GetWindowThreadProcessId(t.window,&pid)&&pid==t.pi.dwProcessId)PostMessageW(t.window,WM_CLOSE,0,0);
    const bool done=WaitForSingleObject(t.pi.hProcess,5000)==WAIT_OBJECT_0;CloseHandle(t.pi.hProcess);t.pi.hProcess=nullptr;return done;
}
static bool below(HWND target,int round,Json& hitRows){
    bool ok=true;
    for(int side=0;side<2;++side)if(windows[side]&&IsWindowVisible(windows[side])){
        RECT r{};if(!GetWindowRect(windows[side],&r))return false;
        const POINT points[]={{(r.left+r.right)/2,(r.top+r.bottom)/2},{r.left+2,(r.top+r.bottom)/2},
            {r.right-3,(r.top+r.bottom)/2},{(r.left+r.right)/2,r.top+2},{(r.left+r.right)/2,r.bottom-3},
            {r.left+2,r.top+2},{r.right-3,r.bottom-3}};
        for(const auto point:points){const HWND hit=WindowFromPoint(point);const bool pass=hit==target;ok=ok&&pass;
            hitRows.push_back({{"round",round},{"surface",side},{"x",point.x},{"y",point.y},{"passed",pass},{"hit",(ULONG_PTR)hit},{"target",(ULONG_PTR)target}});}
    }
    for(int i=0;i<7;++i)if(buttonWindows[i]&&IsWindowVisible(buttonWindows[i])){
        RECT r{};GetWindowRect(buttonWindows[i],&r);const POINT p{(r.left+r.right)/2,(r.top+r.bottom)/2};
        const HWND hit=WindowFromPoint(p);const bool pass=hit==target;ok=ok&&pass;
        hitRows.push_back({{"round",round},{"button",i},{"passed",pass},{"hit",(ULONG_PTR)hit},{"target",(ULONG_PTR)target}});
    }
    return ok;
}

static std::wstring desktopName(HDESK h){wchar_t s[256]{};DWORD n=0;return h&&GetUserObjectInformationW(h,UOI_NAME,s,sizeof(s),&n)?s:L"";}
static std::wstring inputName(){HDESK h=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);auto s=desktopName(h);if(h)CloseDesktop(h);return s;}
static bool above(HWND higher,HWND lower){for(HWND h=GetWindow(lower,GW_HWNDPREV);h;h=GetWindow(h,GW_HWNDPREV))if(h==higher)return true;return false;}
static Json stack(){Json result=Json::array();for(HWND h=GetTopWindow(nullptr);h;h=GetWindow(h,GW_HWNDNEXT)){
    if(h==windows[0])result.push_back("input0");else if(h==windows[1])result.push_back("input1");
    else if(h==outlines[0])result.push_back("outline0");else if(h==outlines[1])result.push_back("outline1");
    else if(h==contactVisuals[0].window)result.push_back("contact0");else if(h==contactVisuals[1].window)result.push_back("contact1");
    else if(h==cursorWindow)result.push_back("cursor");else if(h==fixtureForeground)result.push_back("external");
}return result;}
static void sendContact(int side,UINT32 id,ymcc::screeninput::Phase phase,float x,float y){
    ymcc::screeninput::Event e{side,id,PT_TOUCH,phase,x,y};nativeInputSink(windows[side],e,nullptr);
}
static bool recover(HWND target){fixtureForeground=target;if(!message(target,promoteMessage))return false;ownerVisibilityQueued=true;return consumeOwnerVisibility(ownerVisibilityEpoch);}
static Json pad(){const auto& p=core.pads[0];return {{"id",p.id},{"down",p.down},{"x",p.x},{"y",p.y},{"started",p.started},{"travelSquared",p.travelSquared}};}
static int runOnUIThread(int argc,wchar_t** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(argc==3&&std::wstring(argv[1])==L"--target"){
    HWND h=create(L"YMCC_DeckStackLower",targetProc,WS_EX_TOPMOST,WS_POPUP);if(!h)return 3;
    ShowWindow(h,SW_SHOWNOACTIVATE);SetTimer(h,1,60000,[](HWND h,UINT,UINT_PTR,DWORD){PostMessageW(h,WM_CLOSE,0,0);});
    std::ofstream f{std::filesystem::path(argv[2])};f<<(ULONG_PTR)h;f.close();MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return 0;
 }
 if(argc!=2)return 2;const auto output=std::filesystem::path(argv[1]),directory=output.parent_path();
 Json report={{"ok",false},{"hiddenDesktop",true},{"foregroundAdapter","owned-lower-process HWND fixture"},{"cursorObservationFixture",true},{"eventDeliveryVerified",false},
    {"visibleDesktopSwitched",false},{"systemInputInjected",false},{"systemCursorManipulated",false},{"hardwareWrites",0},{"productDeployed",false},{"actualSteamOrGame",false},{"fieldFlickerVerified",false},{"hitRows",Json::array()},{"stacks",Json::array()}};
 const auto initialInput=inputName();
 const std::wstring name=L"YMCC_DeckStack_R6_"+std::to_wstring(GetCurrentProcessId());HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);fixtureDesktop=desktop;
 if(!desktop||!SetThreadDesktop(desktop)){report["failure"]="private desktop unavailable";std::ofstream(output)<<report.dump(2);return 4;}
 Target targets[2];HWND host=nullptr;bool apartment=false,terminal=true;
 try{
    wchar_t station[1024]{};DWORD bytes=0;if(!GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station,sizeof(station),&bytes))throw std::runtime_error("station unavailable");
    const std::wstring desk=std::wstring(station)+L"\\"+name;
    if(!start(targets[0],directory.wstring(),desk,0)||!start(targets[1],directory.wstring(),desk,1))throw std::runtime_error("owned lower-process startup failed");
    report["targetPids"]={targets[0].pi.dwProcessId,targets[1].pi.dwProcessId};fixtureForeground=targets[0].window;
    winrt::init_apartment(winrt::apartment_type::single_threaded);apartment=true;
    host=create(L"YMCC_DeckStackOwner",ownerProc,0,0,HWND_MESSAGE);if(!host)throw std::runtime_error("owner creation failed");
    dryRun=true;Config cfg;cfg.enabled=true;cfg.left=cfg.right=Mode::Deck;cfg.layout=Layout::Dual;cfg.transparency=80;cfg.touchFeedback=TouchFeedbackMode::Off;cfg.autoHideSeconds=0;
    setContext("steamdeck",true);if(!initializeForOwner(GetModuleHandleW(nullptr),host,cfg,1,L"owned.exe"))throw std::runtime_error("native owner initialization failed");
    for(int round=0;round<4;++round){
        const auto label="round-"+std::to_string(round);cfg.scale=80+round*10;configure(cfg);refresh();pump();
        check(label+"-real-native-input-source-ready",nativeInputSurfaces[0].active()&&nativeInputSurfaces[1].active());
        check(label+"-initial-visibility-dispatch",recover(targets[0].window));pump();
        RECT rc{};GetClientRect(windows[0],&rc);const float x=rc.right*.5f,y=rc.bottom*.5f;POINT origin{};ClientToScreen(windows[0],&origin);
        fixtureCursor.ptScreenPos={origin.x+(LONG)x,origin.y+(LONG)y};fixtureCursor.flags=CURSOR_SUPPRESSED;fixtureCursor.hCursor=nullptr;
        const UINT32 id=1000+round*10;sendContact(0,id,ymcc::screeninput::Phase::Down,x,y);
        check(label+"-deck-down-admitted-with-contact-and-cursor",core.pads[0].down&&contactVisuals[0].visible&&cursorTracking&&cursorDeckOnlyTracking&&cursorVisible);
        report["stacks"].push_back({{"round",round},{"stage","first-down"},{"order",stack()}});
        check(label+"-first-down-cursor-above-contact",above(cursorWindow,contactVisuals[0].window));
        const auto contactBefore=pad(),configBefore=configJson(getConfig());const auto child=nativeInputSurfaces[0].child();
        const auto inputUploads=nativeInputSurfaces[0].uploads,contactUploads=contactVisualUploads,cursorBefore=cursorUploads,outlineBefore=outlineUploadCount;
        const auto eventBefore=eventCount,queriesBefore=fixtureCursorQueries;const auto activeBefore=GetActiveWindow();
        check(label+"-held-visibility-dispatch",recover(targets[1].window));
        check(label+"-recovery-adds-no-cursor-query",fixtureCursorQueries==queriesBefore);pump();
        check(label+"-held-contact-config-source-and-cache-preserved",pad()==contactBefore&&configJson(getConfig())==configBefore&&nativeInputSurfaces[0].child()==child&&nativeInputSurfaces[0].uploads==inputUploads&&contactVisualUploads==contactUploads&&cursorUploads==cursorBefore&&outlineUploadCount==outlineBefore&&eventCount==eventBefore&&GetActiveWindow()==activeBefore);
        report["stacks"].push_back({{"round",round},{"stage","held-after-external-restore"},{"order",stack()}});
        check(label+"-held-feedback-stack-restored",above(contactVisuals[0].window,outlines[0])&&above(contactVisuals[0].window,fixtureForeground)&&above(cursorWindow,contactVisuals[0].window)&&above(cursorWindow,fixtureForeground));
        check(label+"-active-feedback-mouse-pass",below(fixtureForeground,round,report["hitRows"]));
        cursorSyncInfo(fixtureCursor,GetTickCount64(),false); // Settle the existing cache invalidation from the Z-order event.
        const auto raises=ownerVisibilityRaises,positionsBefore=cursorWindowPlacements,positionOnlyBefore=cursorDeckPositionOnlyMoves,moveQueries=fixtureCursorQueries;
        for(int i=0;i<100;++i){sendContact(0,id,ymcc::screeninput::Phase::Move,x+(i%30),y+(i%10));cursorSyncInfo(fixtureCursor,GetTickCount64(),false);}
        check(label+"-100-moves-preserve-layer-order-and-native-cache",above(cursorWindow,contactVisuals[0].window)&&above(contactVisuals[0].window,outlines[0])&&nativeInputSurfaces[0].uploads==inputUploads&&contactVisualUploads==contactUploads&&cursorUploads==cursorBefore&&ownerVisibilityRaises==raises);
        check(label+"-100-moves-add-no-cursor-query-placement-or-raise",fixtureCursorQueries==moveQueries&&cursorWindowPlacements==positionsBefore&&cursorDeckPositionOnlyMoves==positionOnlyBefore);
        sendContact(0,id,ymcc::screeninput::Phase::Up,x,y);
        check(label+"-UP-retains-cursor-and-hides-contact",!core.pads[0].down&&!contactVisuals[0].visible&&cursorRetained&&!cursorTracking&&cursorVisible);
        cursorSyncInfo(fixtureCursor,GetTickCount64(),false); // Complete an ordinary retained sample, without an external raise.
        const auto recontactQueries=fixtureCursorQueries;sendContact(0,id+3,ymcc::screeninput::Phase::Down,x,y);
        check(label+"-plain-recontact-uses-one-existing-query",fixtureCursorQueries==recontactQueries+1);
        report["stacks"].push_back({{"round",round},{"stage","recontact-without-external-event"},{"order",stack()}});
        check(label+"-plain-recontact-cursor-above-contact",cursorVisible&&core.pads[0].down&&above(cursorWindow,contactVisuals[0].window));
        sendContact(0,id+3,ymcc::screeninput::Phase::Up,x,y);
        check(label+"-retained-visibility-dispatch",recover(targets[0].window));pump();
        check(label+"-retained-cursor-restored",cursorRetained&&cursorVisible&&above(cursorWindow,fixtureForeground)&&!contactVisuals[0].visible);
        sendContact(0,id+1,ymcc::screeninput::Phase::Down,x,y);
        report["stacks"].push_back({{"round",round},{"stage","recontact-same-cursor-position"},{"order",stack()}});
        check(label+"-recontact-cursor-remains-above-new-contact",cursorVisible&&core.pads[0].down&&above(cursorWindow,contactVisuals[0].window));
        const auto secondQueries=fixtureCursorQueries;sendContact(1,id+2,ymcc::screeninput::Phase::Down,x,y);
        check(label+"-second-pad-uses-one-existing-query",fixtureCursorQueries==secondQueries+1);
        report["stacks"].push_back({{"round",round},{"stage","second-pad-down"},{"order",stack()}});
        check(label+"-second-pad-cursor-remains-above-both-contacts",core.pads[0].down&&core.pads[1].down&&above(cursorWindow,contactVisuals[0].window)&&above(cursorWindow,contactVisuals[1].window));
        sendContact(1,id+2,ymcc::screeninput::Phase::Cancel,x,y);sendContact(0,id+1,ymcc::screeninput::Phase::Cancel,x,y);
        check(label+"-cancel-clears-all-contact-and-cursor-ownership",!core.pads[0].down&&!core.pads[1].down&&!cursorTracking&&!cursorRetained&&!cursorVisible&&!contactVisuals[0].visible&&!contactVisuals[1].visible);
        check(label+"-cancelled-feedback-mouse-pass",below(fixtureForeground,round,report["hitRows"]));
    }
    report["policyVersion"]=state().at("cursorPolicyVersion");report["cursorState"]=cursorTransitionState();
    report["renderCounters"]={{"cursorObservationQueries",fixtureCursorQueries},{"cursorQueries",cursorQueries},{"cursorWindowPlacements",cursorWindowPlacements},{"cursorPositionOnlyMoves",cursorDeckPositionOnlyMoves},
        {"cursorUploads",cursorUploads},{"contactUploads",contactVisualUploads},{"contactMoves",contactVisualMoves},{"outlineUploads",outlineUploadCount}};
    check("no-real-output-produced",injectedCount==0&&sentKeys==0&&!sentClick);
 }catch(const winrt::hresult_error& e){passed=false;report["failure"]=(long)e.code();}catch(const std::exception& e){passed=false;report["failure"]=e.what();}
 shutdownForOwner();if(host)DestroyWindow(host);fixtureForeground=nullptr;
 for(auto& target:targets)terminal=stop(target)&&terminal;
 // Composition/DispatcherQueue own thread windows. Release them on this UI
 // worker, then let the worker exit before the unchanged main thread closes
 // the desktop. A live UI thread must not pretend SetThreadDesktop succeeded.
 bool compositorClosed=true,queueStopped=true;
 try{if(nativeInputRuntime.compositor)nativeInputRuntime.compositor.as<winrt::Windows::Foundation::IClosable>().Close();}
 catch(...){compositorClosed=false;}
 nativeInputRuntime.compositor=nullptr;
 // Still attempt queue shutdown after a compositor-close failure. A deadline
 // expiry is a failed fixture, never evidence that the queue has stopped.
 try{
    if(nativeInputRuntime.queue){auto pending=nativeInputRuntime.queue.ShutdownQueueAsync();const auto deadline=GetTickCount64()+3000;
        while(pending.Status()==winrt::Windows::Foundation::AsyncStatus::Started&&GetTickCount64()<deadline){pump();MsgWaitForMultipleObjectsEx(0,nullptr,20,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
        queueStopped=pending.Status()==winrt::Windows::Foundation::AsyncStatus::Completed;
        if(queueStopped)pending.GetResults();
    }
 }catch(...){queueStopped=false;}
 const bool runtimeShutdown=compositorClosed&&queueStopped;
 report["runtimeShutdownCompleted"]=runtimeShutdown;report["queueShutdownObservedCompleted"]=queueStopped;
 check("composition-runtime-shutdown-completed",runtimeShutdown);
 if(!queueStopped){
    // This isolated executable has already stopped its two owned children.
    // Do not tear down COM or claim worker/desktop cleanup with a live queue.
    // Retain a failed result and end only this fixture process; the runner
    // rejects it. No installed YMCC, Steam, device or input desktop is touched.
    report["ok"]=false;report["cleanupIncomplete"]=true;report["failure"]="dispatcher-queue-shutdown-not-confirmed";
    report["ownedTargetsTerminal"]=terminal;report["inputDesktopUnchanged"]=initialInput==inputName();report["cases"]=cases;
    {std::ofstream file(output);file<<report.dump(2);file.flush();}
    std::_Exit(4);
 }
 nativeInputRuntime.queue=nullptr;nativeInputRuntime.thread=0;
 if(apartment)winrt::uninit_apartment();
 check("owned-lower-processes-terminal",terminal);
 check("input-desktop-unchanged",!initialInput.empty()&&initialInput==inputName());report["inputDesktopUnchanged"]=initialInput==inputName();report["ownedTargetsTerminal"]=terminal;
 report["cases"]=cases;report["ok"]=passed;std::ofstream file(output);file<<report.dump(2);std::cout<<"NATIVE_DECK_STACK ok="<<passed<<" checks="<<cases.size()<<" hits="<<report["hitRows"].size()<<"\n";return passed&&file.good()?0:3;
}

int wmain(int argc,wchar_t** argv){
 if(argc==3&&std::wstring(argv[1])==L"--target")return runOnUIThread(argc,argv);
 if(argc!=2)return 2;const HDESK original=GetThreadDesktop(GetCurrentThreadId());int result=2;
 std::thread worker([&]{result=runOnUIThread(argc,argv);});worker.join();
 const bool closed=fixtureDesktop&&CloseDesktop(fixtureDesktop);fixtureDesktop=nullptr;
 const bool mainUnchanged=GetThreadDesktop(GetCurrentThreadId())==original;
 const std::filesystem::path output(argv[1]);Json report;{std::ifstream input(output);if(!(input>>report))return 4;}
 report["cases"].push_back({{"name","private-desktop-closed-after-UI-worker-exit"},{"ok",closed}});
 report["cases"].push_back({{"name","main-thread-desktop-unchanged"},{"ok",mainUnchanged}});
 report["uiWorkerTerminated"]=true;report["privateDesktopClosed"]=closed;report["ok"]=report.value("ok",false)&&closed&&mainUnchanged;
 std::ofstream file(output);file<<report.dump(2);return report["ok"].get<bool>()&&file.good()?0:3;
}
