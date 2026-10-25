#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
#include <set>
static std::set<DWORD> rawActive;
static unsigned rawDowns=0,rawUps=0,rawMoves=0;
static LRESULT CALLBACK observedTouch(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_TOUCH){std::array<TOUCHINPUT,16> info{};const UINT count=LOWORD(w);if(count<=info.size()&&GetTouchInputInfo((HTOUCHINPUT)l,count,info.data(),sizeof(TOUCHINPUT))) {
        for(UINT i=0;i<count;++i){const auto& input=info[i];if(input.dwFlags&TOUCHEVENTF_DOWN){rawActive.insert(input.dwID);++rawDowns;}
            if(input.dwFlags&TOUCHEVENTF_UP){rawActive.erase(input.dwID);++rawUps;}if(input.dwFlags&TOUCHEVENTF_MOVE)++rawMoves;}
    }CloseTouchInputHandle((HTOUCHINPUT)l);return 0;}
    return windowProc(h,m,w,l);
}
static bool pair(const OwnTarget& target,DWORD flags) {
    POINTER_TOUCH_INFO info[2]{};for(int i=0;i<2;++i){POINT p{430+i*35,340+i*30};if(!authorizedPoint(target,p))return false;auto& t=info[i];
        t.pointerInfo.pointerType=PT_TOUCH;t.pointerInfo.pointerId=i;t.pointerInfo.pointerFlags=flags;t.pointerInfo.ptPixelLocation=p;
        t.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE|TOUCH_MASK_ORIENTATION;t.rcContact={p.x-3,p.y-3,p.x+3,p.y+3};t.pressure=512;t.orientation=90;}
    return InjectTouchInput(2,info)!=FALSE;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);const HWND foreground=GetForegroundWindow();OwnTarget target;bool valid=true;
    Json result={{"diagnosticValid",false},{"featureAcceptancePassed",false},{"productDeployed",false},{"configurationWrites",0},{"systemOutputInjected",false},{"clickForwarded",false},{"cases",Json::array()}};
    try {if(!target.start())throw std::runtime_error("owned-target-unavailable");if(!InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-injection-unavailable");winrt::init_apartment(winrt::apartment_type::single_threaded);
        for(int implementation=0;implementation<2;++implementation)for(int flags=0;flags<4;++flags) {
            setNativeInputCandidate(false);Config cfg=profileDefault(2);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::DualSense;dryRun=true;setContext("dualsense-edge",true);initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();
            HWND root=windows[0];SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(root,HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]=sizes[1]={260,200};
            if(implementation){setNativeInputCandidate(true);if(!nativeInputSync(0,true,false))throw std::runtime_error("component-attach-failed");}
            HWND child=implementation?nativeInputSurfaces[0].child():nullptr;
            const WNDPROC oldRoot=(WNDPROC)SetWindowLongPtrW(root,GWLP_WNDPROC,(LONG_PTR)observedTouch);WNDPROC oldChild=nullptr;
            if(child)oldChild=(WNDPROC)SetWindowLongPtrW(child,GWLP_WNDPROC,(LONG_PTR)observedTouch);
            const bool registeredRoot=RegisterTouchWindow(root,flags)!=FALSE;const bool registeredChild=!child||RegisterTouchWindow(child,flags)!=FALSE;
            rawActive.clear();rawDowns=rawUps=rawMoves=0;pump(100);const auto before=eventCount;const LONG lower=shared->touchDowns;
            bool injected=pair(target,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);unsigned coreHeld=0,legacyHeld=0;
            for(int step=0;step<25;++step){Sleep(5);injected=pair(target,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(15);
                if(core.pads[0].down&&core.pads[1].down)++coreHeld;if(rawActive.size()==2)++legacyHeld;}
            injected=pair(target,POINTER_FLAG_UP)&&injected;pump(80);
            result["cases"].push_back({{"implementation",implementation?"native-component":"legacy-Win32"},{"registrationFlags",flags},{"registeredRoot",registeredRoot},{"registeredChild",registeredChild},{"inputOk",injected},
                {"coreHeldSamples",coreHeld},{"legacyTouchHeldSamples",legacyHeld},{"rawTouchDowns",rawDowns},{"rawTouchUps",rawUps},{"rawTouchMoves",rawMoves},{"coreEvents",eventCount-before},
                {"lowerTouchDowns",shared->touchDowns-lower},{"released",!core.pads[0].down&&!core.pads[1].down&&rawActive.empty()}});
            valid=valid&&registeredRoot&&registeredChild&&injected&&rawActive.empty();UnregisterTouchWindow(root);if(child)UnregisterTouchWindow(child);
            SetWindowLongPtrW(root,GWLP_WNDPROC,(LONG_PTR)oldRoot);if(child)SetWindowLongPtrW(child,GWLP_WNDPROC,(LONG_PTR)oldChild);shutdown();setNativeInputCandidate(false);pump(30);
        }
    }catch(const std::exception& e){valid=false;result["failure"]=e.what();shutdown();}catch(const winrt::hresult_error& e){valid=false;result["failure"]=(long)e.code();shutdown();}
    result["ownedTargetTerminal"]=target.stop();valid=valid&&result["ownedTargetTerminal"].get<bool>();result["diagnosticValid"]=valid;if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<result.dump(2);std::cout<<"PALM_BUFFERING_PROBE valid="<<valid<<" samples="<<result["cases"].size()<<"\n";return valid&&file.good()?0:2;
}