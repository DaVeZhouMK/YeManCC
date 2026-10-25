#include "screen_touchpad_owner.h"
#include "screen_touchpads_native_topology_test_host.h"
static unsigned appliedRebinds=0;
static LRESULT CALLBACK ownOwnerProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==kOwnerRebindMessage){if(consumeOwnerRebind((UINT_PTR)w))++appliedRebinds;return 0;}
    if(m==kOwnerVisibilityMessage){consumeOwnerVisibility((UINT_PTR)w);return 0;}
    if(m==WM_DISPLAYCHANGE||m==WM_DPICHANGED){refresh(true);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static bool contact(const OwnTarget& target,POINT point,DWORD flags){if(!authorizedPoint(target,point))return false;POINTER_TOUCH_INFO input{};input.pointerInfo.pointerType=PT_TOUCH;input.pointerInfo.pointerId=0;
    input.pointerInfo.pointerFlags=flags;input.pointerInfo.ptPixelLocation=point;input.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE;input.rcContact={point.x-3,point.y-3,point.x+3,point.y+3};input.pressure=512;
    return InjectTouchInput(1,&input)!=FALSE;}
static void placeOwn(const OwnTarget& target){SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(windows[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]=sizes[1]={260,200};if(nativeInputCandidateRequested&&!nativeInputSync(0,true,false))throw std::runtime_error("owner-source-sync-failed");pump(40);}
int wmain(int argc,wchar_t** argv){if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT oldCursor{};GetCursorPos(&oldCursor);const HWND foreground=GetForegroundWindow();OwnTarget target;bool ok=true;Json report={{"ok",false},{"featureAcceptancePassed",false},{"productDeployed",false},{"virtualTargetStarted",false},{"configurationWrites",0},{"systemOutputInjected",false},{"cases",Json::array()}};
    auto check=[&](const char* name,bool passed){ok=ok&&passed;report["cases"].push_back({{"name",name},{"ok",passed}});};
#ifdef YMCC_SCREEN_NATIVE_INPUT_CANDIDATE
    constexpr bool compiled=true;
#else
    constexpr bool compiled=false;
#endif
    report["candidateCompiled"]=compiled;
    try{const wchar_t* candidate=L"owned.exe --screen-touchpads-native-input-candidate";
        const std::array<const wchar_t*,10> lines={L"owned.exe",L"\"C:\\path\\--screen-touchpads-native-input-candidate.exe\"",L"owned.exe --screen-touchpads-native-input-candidate-extra",L"owned.exe --prefix=--screen-touchpads-native-input-candidate",
            candidate,L"owned.exe \"--screen-touchpads-native-input-candidate\"",L"owned.exe --screen-touchpads-native-input-legacy",L"owned.exe --screen-touchpads-native-input-candidate --screen-touchpads-native-input-legacy",L"owned.exe --screen-touchpads-native-input-candidate --screen-touchpads-native-input-candidate",L"owned.exe \"--screen-touchpads-native-input-candidate fake\""};
        for(int i=0;i<(int)lines.size();++i){const auto policy=parseNativeStartup(lines[i]);const bool wanted=i==4||i==5||i==7||i==8;check(("exact-startup-arguments-"+std::to_string(i)).c_str(),policy.parsed&&policy.requested==wanted&&policy.conflict==(i==7));}
        check("null-command-line-refuses-request",!parseNativeStartup(nullptr).requested);check("empty-command-line-refuses-request",!parseNativeStartup(L"").requested);
        if(!target.start())throw std::runtime_error("owned-target-unavailable");if(!InitializeTouchInjection(1,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-unavailable");winrt::init_apartment(winrt::apartment_type::single_threaded);
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YmccOwnTouchpadLifecycle";cls.lpfnWndProc=ownOwnerProc;RegisterClassW(&cls);
        HWND ownerWindow=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);if(!ownerWindow)throw std::runtime_error("owner-window-unavailable");
        Config cfg=profileDefault(0);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;dryRun=true;setContext("disabled",false);
        check("foreign-process-owner-is-rejected",!initializeForOwner(cls.hInstance,target.window,cfg,0,candidate));windowError=0;
        check("default-normal-startup-uses-native-when-compiled",initializeForOwner(cls.hInstance,ownerWindow,cfg,0,L"owned.exe")&&nativeInputCandidateRequested==compiled);report["defaultStartup"]=ownerStartupState();shutdownForOwner();
        check("explicit-legacy-startup-remains-available",initializeForOwner(cls.hInstance,ownerWindow,cfg,0,L"owned.exe --screen-touchpads-native-input-legacy")&&!nativeInputCandidateRequested);shutdownForOwner();
        check("conflicting-startup-flags-remain-legacy",initializeForOwner(cls.hInstance,ownerWindow,cfg,0,L"owned.exe --screen-touchpads-native-input-candidate --screen-touchpads-native-input-legacy")&&!nativeInputCandidateRequested&&nativeStartupConflict);shutdownForOwner();
        check("explicit-owner-startup-requires-build-support",initializeForOwner(cls.hInstance,ownerWindow,cfg,0,candidate)&&nativeInputCandidateRequested==compiled);placeOwn(target);
        const bool nativeActive=state().value("nativeMousePassActive",false);check("owner-state-does-not-fake-active",nativeActive==compiled);
        POINT point{400,310};const LONG mouseBefore=shared->leftDowns,touchBefore=shared->touchDowns;const auto eventsBefore=eventCount;
        bool injected=mouseClickAndWheel(target,point);injected=contact(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;Sleep(3);point.x+=16;point.y+=12;
        injected=contact(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(60);const bool held=core.pads[0].down;
        ownerOnSuspend();check("suspend-releases-existing-contact",injected&&held&&!core.pads[0].down&&!sentKeys&&!sentClick);
        injected=contact(target,point,POINTER_FLAG_UP)&&injected;pump(50);check("old-up-after-suspend-does-not-leak",injected&&!core.pads[0].down&&shared->touchDowns==touchBefore);
        check("actual-owner-route-mouse-contract",shared->leftDowns-mouseBefore==(compiled?1:0)&&eventCount>eventsBefore);
        const auto rebindBefore=appliedRebinds;const auto saved=configJson(getConfig());bool first=queueOwnerResumeRebind(),second=queueOwnerResumeRebind();check("resume-message-coalesces-and-stays-deferred",first==compiled&&second==compiled&&appliedRebinds==rebindBefore);
        pump(80);check("one-resume-rebind-and-unchanged-profile",appliedRebinds-rebindBefore==(compiled?1:0)&&saved==configJson(getConfig()));placeOwn(target);check("already-consumed-resume-message-is-ignored",!consumeOwnerRebind(ownerLifecycleEpoch));
        auto verifyInputAfterOwnerChange=[&](const char* name){
            const auto events=eventCount;const LONG touchLeak=shared->touchDowns,upLeak=shared->touchUps,moveLeak=shared->touchUpdates,clicks=shared->leftDowns,wheels=shared->wheels;
            POINT point{400,310};bool sent=mouseClickAndWheel(target,point);sent=contact(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&sent;
            Sleep(3);point.x+=16;point.y+=12;sent=contact(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&sent;pump(50);
            const bool held=core.pads[0].down;sent=contact(target,point,POINTER_FLAG_UP)&&sent;pump(50);
            check(name,sent&&held&&eventCount-events==3&&!core.pads[0].down&&shared->touchDowns==touchLeak&&shared->touchUps==upLeak&&shared->touchUpdates==moveLeak&&
                shared->leftDowns-clicks==(compiled?1:0)&&shared->wheels-wheels==(compiled?1:0));
        };
        verifyInputAfterOwnerChange("real-pointer-and-mouse-route-after-owner-resume-rebind");
        const LONG resumeLeakDown=shared->touchDowns,resumeLeakUp=shared->touchUps,resumeLeakMove=shared->touchUpdates;
        POINT heldPoint{400,310};bool heldInput=contact(target,heldPoint,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
        Sleep(3);heldPoint.x+=16;heldPoint.y+=12;heldInput=contact(target,heldPoint,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&heldInput;pump(50);
        const bool beforeSuspend=core.pads[0].down;ownerOnSuspend();queueOwnerResumeRebind();pump(70);placeOwn(target);
        const bool afterRebindReleased=!core.pads[0].down&&!sentKeys&&!sentClick;
        heldInput=contact(target,heldPoint,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&heldInput;pump(30);
        heldInput=contact(target,heldPoint,POINTER_FLAG_UP)&&heldInput;pump(50);
        check("old-contact-update-UP-across-resume-rebuild-never-resurrects-or-leaks",heldInput&&beforeSuspend&&afterRebindReleased&&!core.pads[0].down&&!sentKeys&&!sentClick&&
            shared->touchDowns==resumeLeakDown&&shared->touchUps==resumeLeakUp&&shared->touchUpdates==resumeLeakMove);
        verifyInputAfterOwnerChange("new-touch-and-mouse-work-after-held-contact-resume-rebuild");
        const auto stale=ownerLifecycleEpoch;ownerOnSuspend();check("stale-resume-epoch-is-rejected",!consumeOwnerRebind(stale));
        const auto displayBefore=outlineUploadCount;SendMessageW(ownerWindow,WM_DISPLAYCHANGE,0,0);check("display-event-force-rebinds-artwork",outlineUploadCount>displayBefore);placeOwn(target);verifyInputAfterOwnerChange("real-pointer-and-mouse-route-after-display-owner-event");
        const auto dpiBefore=outlineUploadCount;SendMessageW(ownerWindow,WM_DPICHANGED,0,0);check("DPI-event-force-rebinds-artwork",outlineUploadCount>dpiBefore);placeOwn(target);verifyInputAfterOwnerChange("real-pointer-and-mouse-route-after-DPI-owner-event");
        const auto token=ownerLifecycleEpoch;shutdownForOwner();check("shutdown-invalidates-queued-owner-work",!consumeOwnerRebind(token)&&!windows[0]);
        check("same-config-owner-restart-recreates-input",initializeForOwner(cls.hInstance,ownerWindow,cfg,0,candidate)&&windows[0]);placeOwn(target);
        const LONG lower=shared->touchDowns;const auto after=eventCount;point={400,310};injected=contact(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);Sleep(3);point.x+=16;point.y+=12;
        injected=contact(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(50);injected=contact(target,point,POINTER_FLAG_UP)&&injected;pump(50);
        check("owner-restart-still-receives-real-Windows-pointer-events",injected&&eventCount-after==3&&shared->touchDowns==lower&&!core.pads[0].down);
        report["ownerStartup"]=ownerStartupState();shutdownForOwner();DestroyWindow(ownerWindow);
    }catch(const std::exception& e){ok=false;report["failure"]=e.what();shutdownForOwner();}catch(const winrt::hresult_error& e){ok=false;report["failure"]=(long)e.code();shutdownForOwner();}
    report["ownedTargetTerminal"]=target.stop();ok=ok&&report["ownedTargetTerminal"].get<bool>();report["ok"]=ok;POINT now{};GetCursorPos(&now);if(now.x==400&&now.y==310)SetCursorPos(oldCursor.x,oldCursor.y);if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<report.dump(2);std::cout<<"OWNER_LIFECYCLE compiled="<<compiled<<" passed="<<ok<<" checks="<<report["cases"].size()<<"\n";return ok&&file.good()?0:2;
}