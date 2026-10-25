#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
static bool frame(const OwnTarget& target,POINT point,DWORD flags) {
    if(!authorizedPoint(target,point))return false;POINTER_TOUCH_INFO touch{};touch.pointerInfo.pointerType=PT_TOUCH;touch.pointerInfo.pointerId=0;touch.pointerInfo.ptPixelLocation=point;
    touch.pointerInfo.pointerFlags=flags;touch.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;touch.rcContact={point.x-3,point.y-3,point.x+3,point.y+3};touch.orientation=90;touch.pressure=512;
    return InjectTouchInput(1,&touch)!=FALSE;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);const HWND foreground=GetForegroundWindow();OwnTarget target;bool valid=true,passed=true;
    Json result={{"diagnosticValid",false},{"productionMappingsPassed",false},{"featureAcceptancePassed",false},{"systemOutputInjected",false},{"virtualTargetStarted",false},{"productDeployed",false},{"cases",Json::array()}};
    try {if(!target.start())throw std::runtime_error("owned-target-unavailable");if(!InitializeTouchInjection(1,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-injection-unavailable");winrt::init_apartment(winrt::apartment_type::single_threaded);
        for(Mode mode:{Mode::Wasd,Mode::Arrows,Mode::Mouse,Mode::Deck,Mode::DualSense}) {
            setNativeInputCandidate(false);dryRun=true;const bool native=nativePad(mode);setContext(mode==Mode::Deck?"steamdeck":mode==Mode::DualSense?"dualsense-edge":"disabled",native);
            Config cfg=profileDefault(activeProfile.load());cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=mode;initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();
            SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(windows[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]=sizes[1]={260,200};setNativeInputCandidate(true);if(!nativeInputSync(0,true,false))throw std::runtime_error("component-attach-failed");pump(100);
            POINT point{386,300};const LONG lower=shared->touchDowns;bool injected=frame(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);Sleep(3);point.x+=4;point.y+=4;
            injected=frame(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(80);
            const auto current=snapshot();const unsigned expected=mode==Mode::Wasd?5u:mode==Mode::Arrows?80u:0u;const bool output=sentKeys==expected&&core.pads[0].down&&(!native||current.left.down);
            // A transparency-only change must preserve the active contact.
            cfg.transparency=100;const bool reset=configure(cfg);const bool opacity=!reset&&core.pads[0].down&&sentKeys==expected;
            // An admission/persona switch releases before any old UP can act.
            if(native){setContext("disabled",false);Config next=profileDefault(0);next.enabled=true;next.layout=Layout::Single;next.single=Mode::Mouse;configure(next,0);}else windowProc(windows[0],WM_CANCELMODE,0,0);
            const bool released=!core.pads[0].down&&!core.pads[1].down&&!sentKeys&&!sentClick;
            injected=frame(target,point,POINTER_FLAG_UP)&&injected;pump(80);const bool noLateOutput=sentKeys==0&&!sentClick&&!core.pads[0].down&&!core.pads[1].down&&shared->touchDowns==lower;
            const bool row=injected&&output&&opacity&&released&&noLateOutput;passed=passed&&row;valid=valid&&injected;
            result["cases"].push_back({{"mode",modeName(mode)},{"inputOk",injected},{"mappingOutputDuringDown",output},{"transparencyPreservedContact",opacity},{"releaseAtAdmissionOrCancel",released},{"oldUpNoOutputOrLeak",noLateOutput},{"passed",row}});
            shutdown();setNativeInputCandidate(false);pump(30);
        }
    }catch(const std::exception& e){valid=false;passed=false;result["failure"]=e.what();shutdown();}catch(const winrt::hresult_error& e){valid=false;passed=false;result["failure"]=(long)e.code();shutdown();}
    result["ownedTargetTerminal"]=target.stop();valid=valid&&result["ownedTargetTerminal"].get<bool>();result["diagnosticValid"]=valid;result["productionMappingsPassed"]=valid&&passed;
    if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<result.dump(2);
    std::cout<<"COMPONENT_MAPPINGS valid="<<valid<<" passed="<<(valid&&passed)<<" cases="<<result["cases"].size()<<"\n";return valid&&passed&&file.good()?0:2;
}