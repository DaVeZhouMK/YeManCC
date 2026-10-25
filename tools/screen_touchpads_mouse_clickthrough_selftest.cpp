// User scope: ordinary mouse clicks anywhere inside a screen touchpad reach
// the actual lower HWND. This is NOT a simultaneous/static multi-touch test.
#include "screen_touchpad_owner.h"
#include "screen_touchpads_native_topology_test_host.h"
static LRESULT CALLBACK clickOwnerProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==kOwnerRebindMessage){consumeOwnerRebind((UINT_PTR)w);return 0;}
    if(m==kOwnerVisibilityMessage){consumeOwnerVisibility((UINT_PTR)w);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static bool click(const OwnTarget& target,POINT point) {
    if(!target.owns(GetForegroundWindow()))return false;
    DWORD pid=0;const HWND hit=WindowFromPoint(point);GetWindowThreadProcessId(hit,&pid);
    bool owned=target.owns(hit);
    for(int i=0;i<2;++i)owned=owned||(pid==GetCurrentProcessId()&&
        (hit==windows[i]||hit==outlines[i]||(windows[i]&&IsChild(windows[i],hit))));
    RECT lower{};if(!owned||!GetWindowRect(target.window,&lower)||!PtInRect(&lower,point))return false;
    INPUT events[3]{};for(auto& event:events){event.type=INPUT_MOUSE;event.mi.dwExtraInfo=0x594d5047u;}
    const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN);
    const int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    if(width<2||height<2)return false;
    events[0].mi.dx=MulDiv(point.x-x,65535,width-1);events[0].mi.dy=MulDiv(point.y-y,65535,height-1);
    events[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    events[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;events[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    return SendInput((UINT)std::size(events),events,sizeof(INPUT))==std::size(events);
}
static void place(const OwnTarget& target,bool dual,int width=260,int height=200) {
    if(!SetWindowPos(target.window,HWND_TOPMOST,300,220,750,360,SWP_NOACTIVATE|SWP_SHOWWINDOW))throw std::runtime_error("lower-placement-failed");
    for(int i=0;i<(dual?2:1);++i) {
        const int x=360+i*300;
        if(!windows[i]||!outlines[i]||!SetWindowPos(windows[i],HWND_TOPMOST,x,280,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW)||
            !SetWindowPos(outlines[i],HWND_TOPMOST,x,280,width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW))throw std::runtime_error("pad-placement-failed");
        sizes[i]={width,height};if(nativeInputCandidateRequested&&!nativeInputSync(i,true,false))throw std::runtime_error("pad-source-sync-failed");
    }
    pump(40);
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT oldCursor{},lastPoint{};GetCursorPos(&oldCursor);bool sentAny=false;
    const HWND foreground=GetForegroundWindow();OwnTarget target;HWND ownerWindow=nullptr;
    bool valid=true,defaultPassed=true,optInPassed=true,legacyBlocked=true;
    Json report={{"ok",false},{"scope","mouse clicks reach the lower window within the touchpad rectangle"},
        {"featureAcceptancePassed",false},{"hardwareTouchVerified",false},{"simultaneousTouchTested",false},
        {"testMouseInjected",true},{"touchpadSystemOutputInjected",false},{"clickForwarded",false},
        {"globalInputHookInstalled",false},{"virtualTargetStarted",false},{"productDeployed",false},{"cases",Json::array()}};
    auto sample=[&](const char* route,const char* region,POINT point,bool expectPass) {
        const auto down=shared->leftDowns,up=shared->leftUps;
        const bool injected=click(target,point);if(injected){sentAny=true;lastPoint=point;}pump(45);
        const auto downs=shared->leftDowns-down,ups=shared->leftUps-up;const bool passed=downs==1&&ups==1;
        valid=valid&&injected&&downs>=0&&downs<=1&&ups>=0&&ups<=1;
        const bool correct=injected&&(expectPass?passed:downs==0&&ups==0);
        report["cases"].push_back({{"route",route},{"region",region},{"x",point.x},{"y",point.y},
            {"mouseSent",injected},{"lowerDowns",downs},{"lowerUps",ups},{"expectedPass",expectPass},{"ok",correct}});
        return correct;
    };
    auto area=[&](const char* route,bool dual,bool expectPass) {
        bool ok=true;for(int i=0;i<(dual?2:1);++i){RECT r{};if(!GetWindowRect(windows[i],&r))throw std::runtime_error("pad-rectangle-unavailable");
            const int x=(r.left+r.right)/2,y=(r.top+r.bottom)/2;
            const std::array<POINT,9> points={POINT{x,y},POINT{r.left+2,y},POINT{r.right-3,y},POINT{x,r.top+2},POINT{x,r.bottom-3},
                POINT{r.left+2,r.top+2},POINT{r.right-3,r.top+2},POINT{r.left+2,r.bottom-3},POINT{r.right-3,r.bottom-3}};
            for(int j=0;j<(int)points.size();++j){const auto region="pad-"+std::to_string(i)+"-point-"+std::to_string(j);ok=sample(route,region.c_str(),points[j],expectPass)&&ok;}}
        return ok;
    };
    try {
        if(!target.start(true,1000,true))throw std::runtime_error("owned-lower-window-not-foreground");
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YeManMouseClickthroughOwner";cls.lpfnWndProc=clickOwnerProc;
        if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("owner-class-failed");
        ownerWindow=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
        if(!ownerWindow)throw std::runtime_error("owner-window-failed");
        SetWindowPos(target.window,HWND_TOPMOST,300,220,750,360,SWP_NOACTIVATE|SWP_SHOWWINDOW);pump(20);
        valid=sample("no-overlay-control","center",POINT{400,310},true)&&valid;
        Config cfg=profileDefault(0);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;
        cfg.summonPosition=SummonPosition::Off;cfg.specialEnabled=cfg.rearEnabled=false;cfg.specialMask=cfg.rearMask=0;
        dryRun=true;setContext("disabled",false);
        if(!initializeForOwner(cls.hInstance,ownerWindow,cfg,0,L"owned.exe --screen-touchpads-native-input-legacy"))throw std::runtime_error("legacy-owner-init-failed");
        place(target,false);legacyBlocked=area("explicit-legacy",false,false);report["legacyStartup"]=ownerStartupState();shutdownForOwner();
        if(!initializeForOwner(cls.hInstance,ownerWindow,cfg,0,L"owned.exe --screen-touchpads-native-input-candidate"))throw std::runtime_error("opt-in-owner-init-failed");
        place(target,false);optInPassed=area("explicit-native",false,true);report["optInStartup"]=ownerStartupState();shutdownForOwner();
        if(!initializeForOwner(cls.hInstance,ownerWindow,cfg,0,L"owned.exe"))throw std::runtime_error("default-owner-init-failed");
        place(target,false);defaultPassed=area("default-single",false,true);report["defaultStartup"]=ownerStartupState();
        place(target,false,230,160);defaultPassed=area("default-resized",false,true)&&defaultPassed;
        ownerOnSuspend();const bool queued=queueOwnerResumeRebind();pump(70);place(target,false);
        report["resumeRebindQueued"]=queued;defaultPassed=queued&&area("default-resumed",false,true)&&defaultPassed;shutdownForOwner();
        cfg.layout=Layout::Dual;cfg.left=Mode::Mouse;cfg.right=Mode::Wasd;
        if(!initializeForOwner(cls.hInstance,ownerWindow,cfg,0,L"owned.exe"))throw std::runtime_error("dual-owner-init-failed");
        place(target,true);defaultPassed=area("default-dual",true,true)&&defaultPassed;shutdownForOwner();
    }catch(const std::exception& e){valid=false;report["failure"]=e.what();shutdownForOwner();}
    catch(const winrt::hresult_error& e){valid=false;report["failure"]=(long)e.code();shutdownForOwner();}
    if(ownerWindow)DestroyWindow(ownerWindow);
    // Do not redirect focus/cursor if the operator has taken control meanwhile.
    if(target.owns(GetForegroundWindow())&&foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    POINT now{};if(sentAny&&GetCursorPos(&now)&&std::abs(now.x-lastPoint.x)<=1&&std::abs(now.y-lastPoint.y)<=1)SetCursorPos(oldCursor.x,oldCursor.y);
    report["syntheticForegroundBootstrapUsed"]=target.bootstrapUsed;report["ownedTargetTerminal"]=target.stop();valid=valid&&report["ownedTargetTerminal"].get<bool>();
    report["diagnosticValid"]=valid;report["legacyBlocksMouse"]=legacyBlocked;report["explicitNativeMousePassed"]=optInPassed;
    report["defaultMousePassed"]=defaultPassed;const bool ok=valid&&legacyBlocked&&optInPassed&&defaultPassed;
    report["ok"]=ok;report["featureAcceptancePassed"]=ok;
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<report.dump(2);
    std::cout<<"MOUSE_CLICKTHROUGH valid="<<valid<<" default="<<defaultPassed<<" native="<<optInPassed<<" legacyBlocked="<<legacyBlocked<<" cases="<<report["cases"].size()<<"\n";
    return ok&&file.good()?0:2;
}
