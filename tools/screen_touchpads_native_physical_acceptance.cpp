// Human-operated, bounded physical-touch comparison. Never injects input.
#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
static unsigned hardwareTouchEvents=0,upperHardwareMouseEvents=0;static Json provenance=Json::array();
static LRESULT CALLBACK physicalObserved(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_MOUSEMOVE||m==WM_MOUSEWHEEL){INPUT_MESSAGE_SOURCE source{};
        if(GetCurrentInputMessageSource(&source)&&source.deviceType==IMDT_MOUSE&&source.originId==IMO_HARDWARE)++upperHardwareMouseEvents;}
    if(m==WM_POINTERDOWN||m==WM_POINTERUPDATE||m==WM_POINTERUP){INPUT_MESSAGE_SOURCE source{};const bool known=GetCurrentInputMessageSource(&source)!=FALSE;
        if(known&&source.deviceType==IMDT_TOUCH&&source.originId==IMO_HARDWARE)++hardwareTouchEvents;
        if(provenance.size()<12)provenance.push_back({{"known",known},{"device",source.deviceType},{"origin",source.originId}});}
    return windowProc(h,m,w,l);
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);
    if(argc!=3||std::wstring(argv[2])!=L"--manual") {std::cerr<<"Usage: physical-touch.exe <report.json> --manual; requires real fingers and mouse, no unattended run.\n";return 4;}
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);const HWND foreground=GetForegroundWindow();OwnTarget target;bool valid=true;
    Json report={{"manualRunRequested",true},{"hardwareAcceptancePassed",false},{"featureAcceptancePassed",false},{"productDeployed",false},{"virtualTargetStarted",false},{"systemOutputInjected",false},
        {"inputInjectionCalls",0},{"globalHookInstalled",false},{"configurationWrites",0},{"stages",Json::array()}};
    // Match normal main.cpp's pointer policy, not just the early selftest path.
    report["productionMouseInPointerRequested"]=true;report["mouseInPointerEnabled"]=EnableMouseInPointer(TRUE)!=FALSE;
    try {if(!target.start(true,10000))throw std::runtime_error("owned-target-not-foreground-after-user-wait");shared->manualInput=1;winrt::init_apartment(winrt::apartment_type::single_threaded);
        std::cout<<"Two stages, 10 seconds each: keep two fingers still on the blue pad for 1 second, THEN concurrently click/drag/wheel a real mouse inside the pad. No binding or controller output is sent.\n";
        for(int phase=0;phase<2;++phase){Config cfg=profileDefault(2);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::DualSense;cfg.transparency=0;cfg.specialMask=0;cfg.specialEnabled=false;cfg.rearMask=0;cfg.rearEnabled=false;cfg.summonPosition=SummonPosition::Off;cfg.summonEnabled=false;dryRun=true;
            setNativeInputCandidate(phase!=0);setContext("dualsense-edge",true);initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();HWND root=windows[0];
            SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(root,HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]=sizes[1]={260,200};
            if(phase&&!nativeInputSync(0,true,false))throw std::runtime_error("component-attach-failed");SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);renderOutline(0,0,true);
            WNDPROC old=nullptr;winrt::event_token token{};winrt::Windows::UI::Core::CoreIndependentInputSource source{nullptr};
            if(!phase)old=(WNDPROC)SetWindowLongPtrW(root,GWLP_WNDPROC,(LONG_PTR)physicalObserved);
            else {source=nativeInputSurfaces[0].source();token=source.PointerPressed([&](auto const&,auto const& args){INPUT_MESSAGE_SOURCE input{};bool known=GetCurrentInputMessageSource(&input)!=FALSE;
                if(known&&input.deviceType==IMDT_TOUCH&&input.originId==IMO_HARDWARE)++hardwareTouchEvents;
                auto point=args.CurrentPoint();POINTER_INFO info{};const bool got=GetPointerInfo(point.PointerId(),&info)!=FALSE;POINTER_DEVICE_INFO device{};bool deviceGot=got&&GetPointerDevice(info.sourceDevice,&device)!=FALSE;
                if(provenance.size()<12)provenance.push_back({{"known",known},{"device",input.deviceType},{"origin",input.originId},{"getPointerInfo",got},{"getPointerDevice",deviceGot},{"pointerDeviceType",device.pointerDeviceType}});});}
            hardwareTouchEvents=upperHardwareMouseEvents=0;provenance=Json::array();pump(60);
            const LONG clicksBefore=shared->leftDowns,wheelsBefore=shared->wheels,dragsBefore=shared->dragMoves,leakBefore=shared->touchDowns,leakUpBefore=shared->touchUps,leakMoveBefore=shared->touchUpdates,hardwareBefore=shared->hardwareMouseEvents;
            shared->manualPadRect={360,280,620,480};shared->manualTouchHeld=0;
            const LONG jointClickBefore=shared->manualJointClicks,jointWheelBefore=shared->manualJointWheels,jointDragBefore=shared->manualJointDrags;ULONGLONG heldStart=0,longestHold=0;std::array<UINT32,2> ids{};std::array<POINT,2> starts{};bool held=false;
            const auto began=GetTickCount64();bool endedEarly=false;int lastRemaining=-1;
            while(GetTickCount64()-began<10000){pump(8);if(!IsWindow(root)||!target.owns(target.window)){endedEarly=true;break;}
                const bool two=core.pads[0].down&&core.pads[1].down;shared->manualTouchHeld=0;
                if(two){const std::array<UINT32,2> nowIds={core.pads[0].id,core.pads[1].id};
                    if(!held||nowIds!=ids){held=true;heldStart=GetTickCount64();ids=nowIds;for(int i=0;i<2;++i)starts[i]={(LONG)core.pads[i].x,(LONG)core.pads[i].y};}
                    bool stationary=true;for(int i=0;i<2;++i)stationary=stationary&&std::hypot(core.pads[i].x-starts[i].x,core.pads[i].y-starts[i].y)<=4;
                    if(!stationary){heldStart=GetTickCount64();for(int i=0;i<2;++i)starts[i]={(LONG)core.pads[i].x,(LONG)core.pads[i].y};}
                    longestHold=std::max(longestHold,GetTickCount64()-heldStart);shared->manualTouchHeld=GetTickCount64()-heldStart>=800?1:0;
                }else held=false;
                const int remaining=std::max(0,10-(int)((GetTickCount64()-began)/1000));const auto title=(phase?L"候选原生触摸板":L"原 Win32 对照")+std::wstring(L"：先双指静止1秒，再鼠标点击/拖动/滚轮，剩余 ")+std::to_wstring(remaining)+L" 秒";
                if(remaining!=lastRemaining){SetWindowTextW(target.window,title.c_str());RECT caption{0,0,500,55};InvalidateRect(target.window,&caption,TRUE);lastRemaining=remaining;}
            }
            shared->manualTouchHeld=0;
            if(core.pads[0].down||core.pads[1].down){SetWindowTextW(target.window,L"阶段结束，请松开两指；测试窗口将随后关闭/切换");RECT caption{0,0,500,55};InvalidateRect(target.window,&caption,TRUE);
                const auto releaseDeadline=GetTickCount64()+1500;while((core.pads[0].down||core.pads[1].down)&&GetTickCount64()<releaseDeadline)pump(8);}
            const bool releasedBeforeClose=!core.pads[0].down&&!core.pads[1].down;
            const LONG jointClicks=shared->manualJointClicks-jointClickBefore,jointWheels=shared->manualJointWheels-jointWheelBefore,jointDrags=shared->manualJointDrags-jointDragBefore;
            const bool physicalOrigins=hardwareTouchEvents>=2&&(upperHardwareMouseEvents>0||shared->hardwareMouseEvents-hardwareBefore>0);
            const bool observed=!endedEarly&&releasedBeforeClose&&longestHold>=800&&shared->touchDowns==leakBefore&&shared->touchUps==leakUpBefore&&shared->touchUpdates==leakMoveBefore;
            const bool mouseJoint=jointClicks>0&&jointWheels>0&&jointDrags>0;
            report["stages"].push_back({{"implementation",phase?"native-component":"legacy-Win32"},{"endedEarly",endedEarly},{"releasedBeforeWindowClose",releasedBeforeClose},{"twoFingerStationaryHoldMs",longestHold},{"stationaryTolerancePixels",4},{"hardwareTouchEvents",hardwareTouchEvents},
                {"hardwareMouseEvents",shared->hardwareMouseEvents-hardwareBefore},{"upperHardwareMouseEvents",upperHardwareMouseEvents},{"hardwareOriginsVerified",physicalOrigins},{"stationaryObserved",observed},{"mouseJointObserved",mouseJoint},{"jointMouseRestrictedToPadRectangle",true},{"jointMouseRequiresStationaryHoldMs",800},{"jointClicks",jointClicks},{"jointWheelEvents",jointWheels},{"jointDragMoves",jointDrags},
                {"lowerClicks",shared->leftDowns-clicksBefore},{"lowerWheels",shared->wheels-wheelsBefore},{"lowerDrags",shared->dragMoves-dragsBefore},{"rawTouchLeaks",shared->touchDowns-leakBefore},{"rawTouchUpLeaks",shared->touchUps-leakUpBefore},{"rawTouchMoveLeaks",shared->touchUpdates-leakMoveBefore},{"provenance",provenance}});
            if(source){source.PointerPressed(token);source=nullptr;}if(old&&IsWindow(root))SetWindowLongPtrW(root,GWLP_WNDPROC,(LONG_PTR)old);shutdown();setNativeInputCandidate(false);valid=valid&&!endedEarly&&releasedBeforeClose;
        }
        if(!dryRun||injectedCount!=0)throw std::runtime_error("manual-output-suppression-invariant-failed");
        const auto& legacy=report["stages"][0];const auto& candidate=report["stages"][1];report["hardwareAcceptancePassed"]=legacy["stationaryObserved"].get<bool>()&&legacy["hardwareOriginsVerified"].get<bool>()&&candidate["stationaryObserved"].get<bool>()&&candidate["mouseJointObserved"].get<bool>()&&candidate["hardwareOriginsVerified"].get<bool>();
    }catch(const std::exception& e){valid=false;report["failure"]=e.what();shutdown();}catch(const winrt::hresult_error& e){valid=false;report["failure"]=(long)e.code();shutdown();}
    report["ownedTargetTerminal"]=target.stop();valid=valid&&report["ownedTargetTerminal"].get<bool>();report["testRunValid"]=valid;if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<report.dump(2);std::cout<<"MANUAL_PHYSICAL_INPUT hardwareAcceptancePassed="<<report["hardwareAcceptancePassed"]<<" featureAcceptancePassed=false\n";return valid&&file.good()&&report["hardwareAcceptancePassed"].get<bool>()?0:3;
}