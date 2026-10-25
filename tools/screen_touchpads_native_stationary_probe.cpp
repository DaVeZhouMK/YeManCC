#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
static bool stationaryFrame(const OwnTarget& target,POINT base,DWORD flags,int reportKind,int step,int count=2) {
    POINTER_TOUCH_INFO contacts[2]{};
    for(int i=0;i<count;++i){auto& contact=contacts[i];POINT point{base.x+i*35,base.y+i*30};if(!authorizedPoint(target,point))return false;
        contact.pointerInfo.pointerType=PT_TOUCH;contact.pointerInfo.pointerId=i;contact.pointerInfo.pointerFlags=flags;contact.pointerInfo.ptPixelLocation=point;
        contact.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;contact.pressure=512+(reportKind==1?(step%2):0);contact.orientation=90+(reportKind==3?(step%2):0);
        const int radius=3+(reportKind==2?(step%2):0);contact.rcContact={point.x-radius,point.y-radius,point.x+radius,point.y+radius};
        if(reportKind==4){LARGE_INTEGER counter{};QueryPerformanceCounter(&counter);contact.pointerInfo.PerformanceCount=counter.QuadPart;}
    }
    if(reportKind==4)contacts[1].pointerInfo.PerformanceCount=contacts[0].pointerInfo.PerformanceCount;
    if(reportKind>=5&&step==1&&flags&POINTER_FLAG_UPDATE){contacts[1].pointerInfo.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;}
    return InjectTouchInput(count,contacts)!=FALSE;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);POINT previous{};GetCursorPos(&previous);const auto foreground=GetForegroundWindow();
    Json result={{"diagnosticValid",false},{"featureAcceptancePassed",false},{"productDeployed",false},{"virtualTargetStarted",false},{"systemOutputInjected",false},{"clickForwarded",false},{"cases",Json::array()}};
    OwnTarget target;bool valid=true;
    try {if(!target.start())throw std::runtime_error("owned-target-unavailable");if(!InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-injection-unavailable");
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        for(int implementation=0;implementation<2;++implementation)for(int kind=0;kind<7;++kind) {
            setNativeInputCandidate(false);Config cfg=profileDefault(2);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::DualSense;dryRun=true;
            setContext("dualsense-edge",true);initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();
            const HWND root=windows[0];SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(root,HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]=sizes[1]={260,200};
            if(implementation){setNativeInputCandidate(true);if(!nativeInputSync(0,true,false))throw std::runtime_error("component-unavailable");}pump(100);
            const POINT point{430,340};const auto eventBefore=eventCount;const LONG leakedBefore=shared->touchDowns;bool injected=stationaryFrame(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT,kind,0,kind>=5?1:2);
            if(kind>=5)pump(kind==5?40:120);
            unsigned activeSamples=0,samples=0;ULONGLONG firstHeldMs=0;const auto began=GetTickCount64();
            for(int step=1;step<=25;++step){Sleep(5);injected=stationaryFrame(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT,kind,step)&&injected;pump(15);
                const bool held=core.pads[0].down&&core.pads[1].down;if(held){++activeSamples;if(!firstHeldMs)firstHeldMs=GetTickCount64()-began;}++samples;}
            const bool heldAtEnd=core.pads[0].down&&core.pads[1].down;
            injected=stationaryFrame(target,point,POINTER_FLAG_UP,kind,26)&&injected;pump(80);
            const bool released=!core.pads[0].down&&!core.pads[1].down;const LONG leaked=shared->touchDowns-leakedBefore;
            result["cases"].push_back({{"implementation",implementation?"native-component":"legacy-Win32"},{"reportKind",kind},{"inputOk",injected},{"coordinatesUnchanged",true},{"heldSamples",activeSamples},{"samples",samples},
                {"firstHeldMs",firstHeldMs},{"heldAtEnd",heldAtEnd},{"released",released},{"lowerTouchDowns",leaked},{"nativeEvents",eventCount-eventBefore},{"stationaryAcceptance",injected&&heldAtEnd&&released&&leaked==0}});
            valid=valid&&injected&&released;shutdown();setNativeInputCandidate(false);pump(30);
        }
    }catch(const std::exception& error){valid=false;result["failure"]=error.what();shutdown();}catch(const winrt::hresult_error& error){valid=false;result["failure"]=(long)error.code();shutdown();}
    result["ownedTargetTerminal"]=target.stop();valid=valid&&result["ownedTargetTerminal"].get<bool>();result["diagnosticValid"]=valid;
    if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground); // This probe never moves the mouse.
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<result.dump(2);std::cout<<"STATIONARY_PROBE valid="<<valid<<" cases="<<result["cases"].size()<<"\n";return valid&&file.good()?0:2;
}