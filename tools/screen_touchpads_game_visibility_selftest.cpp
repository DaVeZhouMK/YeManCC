// Isolated cross-process borderless/topmost-game reproduction. Synthetic
// pointer/mouse controls only; not an actual game or hardware acceptance.
#include "screen_touchpad_owner.h"
#include "screen_touchpads_native_topology_test_host.h"
static Json rawEvents=Json::array();
static HWND traceTarget=nullptr;
static void CALLBACK traceEvent(HWINEVENTHOOK,DWORD event,HWND h,LONG object,LONG child,DWORD thread,DWORD){
    if(!h||(h!=traceTarget&&h!=GetDesktopWindow()&&event!=EVENT_SYSTEM_FOREGROUND))return;
    DWORD pid=0;GetWindowThreadProcessId(h,&pid);rawEvents.push_back({{"event",event},{"hwnd",(uintptr_t)h},{"object",object},{"child",child},{"pid",pid},{"thread",thread},{"ownPid",GetCurrentProcessId()}});
}
static LRESULT CALLBACK visibilityOwnerProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==kOwnerVisibilityMessage){consumeOwnerVisibility((UINT_PTR)w);return 0;}
    if(m==kOwnerRebindMessage){consumeOwnerRebind((UINT_PTR)w);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static bool touch(const OwnTarget& target,POINT p,DWORD flags){
    if(!authorizedPoint(target,p)||!target.owns(GetForegroundWindow()))return false;
    POINTER_TOUCH_INFO in{};in.pointerInfo.pointerType=PT_TOUCH;in.pointerInfo.pointerId=0;
    in.pointerInfo.pointerFlags=flags;in.pointerInfo.ptPixelLocation=p;
    in.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE;in.rcContact={p.x-3,p.y-3,p.x+3,p.y+3};in.pressure=512;
    return InjectTouchInput(1,&in)!=FALSE;
}
static bool hiddenBy(HWND layer,HWND game){return screenLayerOccluded(layer,game);}
static bool promote(const OwnTarget& target){return target.owns(target.window)&&PostMessageW(target.window,kOwnedTargetPromoteMessage,0x594d5047u,0); }
static COLORREF pixel(POINT p){HDC dc=GetDC(nullptr);const auto color=GetPixel(dc,p.x,p.y);ReleaseDC(nullptr,dc);return color;}
int wmain(int argc,wchar_t** argv){
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    OwnTarget target;HWND ownerWindow=nullptr;std::array<HWINEVENTHOOK,4> traceHooks{};POINT oldCursor{},lastMouse{400,310};GetCursorPos(&oldCursor);const HWND foreground=GetForegroundWindow();
    bool ok=true,valid=true;Json report={{"ok",false},{"actualGame",false},{"hardwareTouchVerified",false},{"testInputInjected",true},
        {"virtualTargetStarted",false},{"productDeployed",false},{"periodicTimerAdded",false},{"cases",Json::array()}};
    auto check=[&](const char* name,bool passed){ok=ok&&passed;report["cases"].push_back({{"name",name},{"ok",passed}});};
    try{
        if(!target.start(true,1000,true))throw std::runtime_error("owned-game-window-not-foreground");
        traceTarget=target.window;
        constexpr DWORD traceKinds[]={EVENT_SYSTEM_FOREGROUND,EVENT_OBJECT_REORDER,EVENT_OBJECT_LOCATIONCHANGE,EVENT_OBJECT_SHOW};
        for(int i=0;i<4;++i)traceHooks[i]=SetWinEventHook(traceKinds[i],traceKinds[i],nullptr,traceEvent,0,0,WINEVENT_OUTOFCONTEXT|WINEVENT_SKIPOWNPROCESS);
        if(!InitializeTouchInjection(1,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-fixture-unavailable");
        winrt::init_apartment(winrt::apartment_type::single_threaded);WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YeManVisibilityTestOwner";cls.lpfnWndProc=visibilityOwnerProc;RegisterClassW(&cls);
        ownerWindow=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
        if(!ownerWindow)throw std::runtime_error("owned-owner-unavailable");
        Config cfg=profileDefault(0);cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Mouse;cfg.standaloneSpecialMode=StandaloneSpecialMode::SteamDeck;
        dryRun=true;setContext("disabled",false);
        if(!initializeForOwner(cls.hInstance,ownerWindow,cfg,0,L"owned.exe"))throw std::runtime_error("owner-init-failed");
        SetWindowPos(target.window,HWND_TOPMOST,300,220,800,500,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        for(int i=0;i<2;++i){SetWindowPos(windows[i],HWND_TOPMOST,360+i*300,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(outlines[i],HWND_TOPMOST,360+i*300,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[i]={260,200};if(!nativeInputSync(i,true,false)||!renderOutline(i,cfg.transparency,true))throw std::runtime_error("source-sync-failed");}
        for(int i=1;i<=2;++i){if(!buttonWindows[i]||!buttonOutlines[i])throw std::runtime_error("dedicated-button-fixture-missing");
            SetWindowPos(buttonWindows[i],HWND_TOPMOST,360+(i-1)*160,520,120,54,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(buttonOutlines[i],HWND_TOPMOST,360+(i-1)*160,520,120,54,SWP_NOACTIVATE|SWP_SHOWWINDOW);if(!buttonNativeInputSync(i,true,false))throw std::runtime_error("button-source-sync-failed");}
        pump(80);check("visible-controls-install-event-watchers",ownerVisibilityActive());
        const POINT border{361,330};const auto visiblePixel=pixel(border);
        // Negative control: the existing topmost style is NOT sufficient once
        // the game promotes itself after the touchpad was created.
        stopOwnerVisibility();if(!promote(target))throw std::runtime_error("owned-game-promote-failed");pump(80);
        check("reproduces-game-covering-input-and-outline-without-repair",hiddenBy(windows[0],target.window)&&hiddenBy(outlines[0],target.window));
        const auto coveredPixel=pixel(border);report["visibleBorderPixel"]=visiblePixel;report["coveredBorderPixel"]=coveredPixel;check("covered-border-really-disappears-in-desktop-composition",visiblePixel!=CLR_INVALID&&coveredPixel!=CLR_INVALID&&visiblePixel!=coveredPixel);
        syncOwnerVisibility();pump(80);
        check("event-repair-restores-visible-border-pixel",pixel(border)==visiblePixel);
        check("both-pads-and-button-layers-above-game",!hiddenBy(windows[0],target.window)&&!hiddenBy(outlines[0],target.window)&&!hiddenBy(windows[1],target.window)&&!hiddenBy(outlines[1],target.window)&&!hiddenBy(buttonWindows[1],target.window)&&!hiddenBy(buttonOutlines[1],target.window));
        const auto lowerTouch=shared->touchDowns;POINT point{400,310};bool injected=touch(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);Sleep(3);point.x+=15;injected=touch(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(60);
        valid=valid&&injected;check("controlled-touch-is-held-before-game-promotes-again",injected&&core.pads[0].down);
        const auto events=eventCount,uploads=outlineUploadCount,raises=ownerVisibilityRaises;
        const auto child=nativeInputSurfaces[0].child();const auto config=configJson(getConfig());RECT before{};GetWindowRect(windows[0],&before);
        for(int round=0;round<3;++round){if(!promote(target))throw std::runtime_error("owned-game-promote-failed");pump(90);
            DWORD fgPid=0;const HWND fg=GetForegroundWindow();GetWindowThreadProcessId(fg,&fgPid);
            report["promotionRounds"].push_back({{"round",round},{"foregroundPid",fgPid},{"targetForeground",target.owns(fg)},
                {"raises",ownerVisibilityRaises},{"events",ownerVisibilityEvents},{"inputOccluded",hiddenBy(windows[0],target.window)},{"outlineOccluded",hiddenBy(outlines[0],target.window)}});
            if(!target.owns(fg)){valid=false;throw std::runtime_error("owned-game-lost-foreground-during-test");}
        }
        RECT after{};GetWindowRect(windows[0],&after);
        check("late-game-Z-order-events-repair-without-settings-refresh",ownerVisibilityRaises>raises&&!hiddenBy(windows[0],target.window)&&!hiddenBy(outlines[0],target.window));
        check("repair-does-not-cancel-or-replay-held-touch",core.pads[0].down&&eventCount==events&&shared->touchDowns==lowerTouch);
        check("repair-does-not-recreate-input-or-reupload-visuals",nativeInputSurfaces[0].child()==child&&outlineUploadCount==uploads);
        check("repair-keeps-geometry-config-and-game-focus",EqualRect(&before,&after)&&config==configJson(getConfig())&&target.owns(GetForegroundWindow()));
        injected=touch(target,point,POINTER_FLAG_UP)&&injected;pump(60);valid=valid&&injected;check("UP-after-repair-releases-and-never-leaks-raw-touch",!core.pads[0].down&&shared->touchDowns==lowerTouch);
        const auto downs=shared->leftDowns,ups=shared->leftUps;const bool mouse=mouseClickAndWheel(target,lastMouse);pump(50);valid=valid&&mouse;
        check("mouse-still-clicks-the-lower-game-window",mouse&&shared->leftDowns-downs==1&&shared->leftUps-ups==1);
        const auto stable=ownerVisibilityRaises;for(int i=0;i<100;++i)queueOwnerVisibility();pump(60);
        check("repeated-maintenance-on-correct-stack-does-not-raise",ownerVisibilityRaises==stable);
        const auto checks=ownerVisibilityChecks;pump(160);check("no-idle-polling-work",ownerVisibilityChecks==checks);
        cfg.enabled=false;cfg.standaloneSpecialMode=StandaloneSpecialMode::Off;cfg.specialEnabled=cfg.rearEnabled=false;cfg.specialMask=cfg.rearMask=0;cfg.summonPosition=SummonPosition::Off;configure(cfg,0);pump(30);
        check("disabled-controls-remove-all-event-watchers",!ownerVisibilityActive()&&!ownerVisibilityQueued);
        const auto stale=ownerVisibilityEpoch;shutdownForOwner();check("shutdown-rejects-late-display-work",!ownerVisibilityActive()&&!consumeOwnerVisibility(stale));
    }catch(const std::exception& e){valid=false;ok=false;report["failure"]=e.what();shutdownForOwner();}catch(const winrt::hresult_error& e){valid=false;ok=false;report["failure"]=(long)e.code();shutdownForOwner();}
    for(auto hook:traceHooks)if(hook)UnhookWinEvent(hook);report["rawEvents"]=rawEvents;report["targetPid"]=target.child.dwProcessId;report["targetWindow"]=(uintptr_t)target.window;report["desktop"]=(uintptr_t)GetDesktopWindow();
    if(ownerWindow)DestroyWindow(ownerWindow);
    if(target.owns(GetForegroundWindow())&&foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    POINT now{};if(GetCursorPos(&now)&&std::abs(now.x-lastMouse.x)<=1&&std::abs(now.y-lastMouse.y)<=1)SetCursorPos(oldCursor.x,oldCursor.y);
    report["syntheticForegroundBootstrapUsed"]=target.bootstrapUsed;report["ownedTargetTerminal"]=target.stop();valid=valid&&report["ownedTargetTerminal"].get<bool>();report["diagnosticValid"]=valid;report["ok"]=valid&&ok;report["raises"]=ownerVisibilityRaises;report["events"]=ownerVisibilityEvents;report["checks"]=ownerVisibilityChecks;
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<report.dump(2);std::cout<<"GAME_VISIBILITY_OK="<<(valid&&ok)<<" checks="<<report["cases"].size()<<" actualGame=false\n";return valid&&ok&&file.good()?0:2;
}
