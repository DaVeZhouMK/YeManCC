// Real production binding output to an authenticated owned receiver only.
// It is NOT the mouse-passthrough proof; physical mouse is never replayed.
#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
#pragma comment(lib,"advapi32.lib")
static OwnTarget* outputTarget=nullptr;static bool focusLost=false;
static bool modifiersHeld(){for(UINT key:{VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN})if(GetAsyncKeyState(key)&0x8000)return true;return false;}
static void admittedOutput(HWND root,const ymcc::screeninput::Event& event,void*) {
    if(!outputTarget||!outputTarget->owns(GetForegroundWindow())||modifiersHeld()) {
        focusLost=true;handleCaptureLoss(root,event.surface,event.id);return; // Compensating release only, never a new DOWN/click.
    }
    nativeInputSink(root,event,nullptr);
}
static bool frame(const OwnTarget& target,POINT point,DWORD flags) {
    if(!target.owns(GetForegroundWindow())||!authorizedPoint(target,point)||modifiersHeld()){focusLost=true;return false;}
    POINTER_TOUCH_INFO info{};info.pointerInfo.pointerType=PT_TOUCH;info.pointerInfo.pointerId=0;info.pointerInfo.pointerFlags=flags;info.pointerInfo.ptPixelLocation=point;
    info.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE|TOUCH_MASK_ORIENTATION;info.rcContact={point.x-3,point.y-3,point.x+3,point.y+3};info.pressure=512;info.orientation=90;
    return InjectTouchInput(1,&info)!=FALSE;
}
static bool aimMouse(const OwnTarget& target,POINT point) {
    if(!target.owns(GetForegroundWindow())||!authorizedPoint(target,point))return false;
    INPUT input{};input.type=INPUT_MOUSE;input.mi.dwExtraInfo=0x594d5047u;
    const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),w=GetSystemMetrics(SM_CXVIRTUALSCREEN),h=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    input.mi.dx=MulDiv(point.x-x,65535,w-1);input.mi.dy=MulDiv(point.y-y,65535,h-1);input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    return SendInput(1,&input,sizeof(input))==1;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);POINT oldCursor{};GetCursorPos(&oldCursor);const HWND foreground=GetForegroundWindow();OwnTarget target;bool valid=true,passed=true;POINT lastControlled{};
    Json report={{"diagnosticValid",false},{"realBindingOutputPassed",false},{"featureAcceptancePassed",false},{"productDeployed",false},{"virtualTargetStarted",false},{"clickForwarded",false},{"scope","preserve-production-bindings-not-mouse-passthrough"},
        {"systemBindingOutputEnabled",true},{"hardwareTouchClaimed",false},{"physicalModifierGuard",true},{"cases",Json::array()}};
    try {
        static constexpr UINT guardedKeys[]={'W','S','A','D',VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_SHIFT,VK_CONTROL,VK_MENU,VK_LWIN,VK_RWIN};for(UINT key:guardedKeys)if(GetAsyncKeyState(key)&0x8000)throw std::runtime_error("binding-key-already-held-refuse-test");
        HANDLE token=nullptr;DWORD uiAccess=1,size=0;if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token))throw std::runtime_error("token-query-failed");
        const bool tokenRead=GetTokenInformation(token,TokenUIAccess,&uiAccess,sizeof(uiAccess),&size)!=FALSE;CloseHandle(token);if(!tokenRead||uiAccess)throw std::runtime_error("unexpected-UIAccess-token-refuse-test");report["uiAccess"]=false;
        if(!target.start())throw std::runtime_error("owned-output-target-unavailable");outputTarget=&target;
        if(!InitializeTouchInjection(1,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-injection-unavailable");winrt::init_apartment(winrt::apartment_type::single_threaded);
        for(Mode mode:{Mode::Wasd,Mode::Arrows,Mode::Mouse})for(int stop=0;stop<4;++stop) {
            setNativeInputCandidate(false);setContext("disabled",false);dryRun=false;focusLost=false;
            Config cfg=profileDefault(0);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=mode;cfg.mouseSensitivity=100;cfg.specialMask=cfg.rearMask=0;cfg.specialEnabled=cfg.rearEnabled=false;
            initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();HWND root=windows[0];
            SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(root,HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]=sizes[1]={260,200};setNativeInputCandidate(true);
            if(!nativeInputSurfaces[0].attach(nativeInputRuntime,root,0,admittedOutput,nullptr))throw std::runtime_error("candidate-output-source-unavailable");pump(80);
            if(!aimMouse(target,{450,350}))throw std::runtime_error("owned-cursor-position-guard-failed");pump(30);GetCursorPos(&lastControlled);
            std::array<LONG,8> downBefore{},upBefore{};for(int i=0;i<8;++i){downBefore[i]=shared->bindingDowns[i];upBefore[i]=shared->bindingUps[i];}
            const LONG moveBefore=shared->bindingMouseMoves,clickBefore=shared->bindingMouseDowns,clickUpBefore=shared->bindingMouseUps,originBefore=shared->bindingOriginInjected;
            const LONG touchBefore=shared->touchDowns,touchUpBefore=shared->touchUps,touchMoveBefore=shared->touchUpdates;
            const unsigned expected=mode==Mode::Wasd?5u:mode==Mode::Arrows?80u:0u;POINT point{386,300};
            bool injected=frame(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);Sleep(3);point.x+=4;point.y+=4;
            injected=frame(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(70);
            const bool held=core.pads[0].down&&sentKeys==expected&&shared->bindingHeldMask==(LONG)expected;
            const auto repeatStart=GetTickCount64();while(GetTickCount64()-repeatStart<40){Sleep(3);injected=frame(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(8);}
            bool downOnce=true;for(int i=0;i<8;++i)downOnce=downOnce&&shared->bindingDowns[i]-downBefore[i]==((expected&(1u<<i))?1:0);
            point.x+=12;point.y+=10;Sleep(3);injected=frame(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(70);
            if(mode==Mode::Mouse&&shared->bindingMouseMoves>moveBefore)lastControlled={shared->bindingMouseX,shared->bindingMouseY};
            const bool mouseMoved=mode!=Mode::Mouse||(shared->bindingMouseMoves-moveBefore>=1&&lastControlled.x>450&&lastControlled.y>350);
            if(stop==1){if(!target.owns(GetForegroundWindow()))throw std::runtime_error("focus-changed-before-cancel");windowProc(root,WM_CANCELMODE,0,0);}
            if(stop==2){if(!target.owns(GetForegroundWindow()))throw std::runtime_error("focus-changed-before-disable");Config off=cfg;off.enabled=false;configure(off);}
            if(stop==3){if(!target.owns(GetForegroundWindow()))throw std::runtime_error("focus-changed-before-shutdown");shutdown();}
            injected=frame(target,point,POINTER_FLAG_UP)&&injected;pump(80);
            bool releaseOnce=true;for(int i=0;i<8;++i)releaseOnce=releaseOnce&&shared->bindingUps[i]-upBefore[i]==((expected&(1u<<i))?1:0);
            const bool clean=!sentKeys&&!sentClick&&!shared->bindingHeldMask&&!core.pads[0].down&&!core.pads[1].down;
            const bool noTouchLeak=shared->touchDowns==touchBefore&&shared->touchUps==touchUpBefore&&shared->touchUpdates==touchMoveBefore;
            const bool noUnexpectedClick=shared->bindingMouseDowns==clickBefore&&shared->bindingMouseUps==clickUpBefore;
            const bool row=injected&&!focusLost&&held&&downOnce&&releaseOnce&&clean&&noTouchLeak&&noUnexpectedClick&&mouseMoved&&shared->bindingOriginInjected>originBefore;
            valid=valid&&injected&&!focusLost;passed=passed&&row;
            report["cases"].push_back({{"mode",modeName(mode)},{"stop",stop},{"inputOk",injected},{"ownedForegroundMaintained",!focusLost},{"heldInRealReceiver",held},{"oneDownPerKey",downOnce},{"oneUpPerKey",releaseOnce},{"releasedInRealReceiver",clean},
                {"mouseMovementReceived",mouseMoved},{"taggedMouseMoves",shared->bindingMouseMoves-moveBefore},{"outputOriginInjectedEvents",shared->bindingOriginInjected-originBefore},{"rawTouchLeak",!noTouchLeak},{"unexpectedClick",!noUnexpectedClick},{"passed",row}});
            shutdown();setNativeInputCandidate(false);pump(20);if(!valid)throw std::runtime_error("owned-input-admission-lost");
        }
        // Deliberate touch-to-mouse tap mapping, never evidence of physical-mouse passthrough.
        Config cfg=profileDefault(0);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;dryRun=false;initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();HWND root=windows[0];
        SetWindowPos(root,HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]=sizes[1]={260,200};setNativeInputCandidate(true);
        if(!nativeInputSurfaces[0].attach(nativeInputRuntime,root,0,admittedOutput,nullptr))throw std::runtime_error("tap-source-unavailable");pump(60);
        const LONG clicks=shared->bindingMouseDowns,ups=shared->bindingMouseUps,lowerTouch=shared->touchDowns;POINT point{430,340};bool injected=frame(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);Sleep(3);
        injected=frame(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(30);injected=frame(target,point,POINTER_FLAG_UP)&&injected;pump(60);
        const bool tap=injected&&!focusLost&&shared->bindingMouseDowns-clicks==1&&shared->bindingMouseUps-ups==1&&shared->touchDowns==lowerTouch&&!sentClick;
        report["touchMouseTapMappingPassed"]=tap;valid=valid&&injected;passed=passed&&tap;shutdown();setNativeInputCandidate(false);
    }catch(const std::exception& e){valid=false;passed=false;report["failure"]=e.what();shutdown();}catch(const winrt::hresult_error& e){valid=false;passed=false;report["failure"]=(long)e.code();shutdown();}
    const bool restoreFocus=target.owns(GetForegroundWindow());report["ownedTargetTerminal"]=target.stop();outputTarget=nullptr;dryRun=true;
    valid=valid&&report["ownedTargetTerminal"].get<bool>();report["diagnosticValid"]=valid;report["realBindingOutputPassed"]=valid&&passed;
    POINT now{};GetCursorPos(&now);if(now.x==lastControlled.x&&now.y==lastControlled.y)SetCursorPos(oldCursor.x,oldCursor.y);if(restoreFocus&&foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<report.dump(2);std::cout<<"REAL_BINDINGS valid="<<valid<<" passed="<<(valid&&passed)<<" cases="<<report["cases"].size()<<"\n";return valid&&passed&&file.good()?0:2;
}