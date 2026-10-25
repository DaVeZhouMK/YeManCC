#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
static bool contactPair(const OwnTarget& target,DWORD flags) {
    POINTER_TOUCH_INFO t[2]{};for(int i=0;i<2;++i){POINT p{430+i*35,340+i*30};if(!authorizedPoint(target,p))return false;
        t[i].pointerInfo.pointerType=PT_TOUCH;t[i].pointerInfo.pointerId=i;t[i].pointerInfo.pointerFlags=flags;t[i].pointerInfo.ptPixelLocation=p;
        t[i].touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE|TOUCH_MASK_ORIENTATION;t[i].rcContact={p.x-3,p.y-3,p.x+3,p.y+3};t[i].pressure=512;t[i].orientation=90;}
    return InjectTouchInput(2,t)!=FALSE;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);POINT oldCursor{};GetCursorPos(&oldCursor);const HWND foreground=GetForegroundWindow();OwnTarget target;bool valid=true;
    Json result={{"diagnosticValid",false},{"featureAcceptancePassed",false},{"productDeployed",false},{"systemOutputInjected",false},{"clickForwarded",false},{"cases",Json::array()}};
    try {if(!target.start())throw std::runtime_error("own-target-unavailable");if(!InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-unavailable");winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Windows::System::DispatcherQueueController queue{nullptr};DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};winrt::check_hresult(CreateDispatcherQueueController(options,(ABI::Windows::System::IDispatcherQueueController**)winrt::put_abi(queue)));
        winrt::Windows::UI::Composition::Compositor compositor;
        for(int policy=0;policy<4;++policy){setNativeInputCandidate(false);Config cfg=profileDefault(2);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::DualSense;dryRun=true;setContext("dualsense-edge",true);initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();
            HWND root=windows[0];SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(root,HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]=sizes[1]={260,200};
            HWND child=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"YeManScreenTouchpad",L"owned palm policy child",WS_CHILD|WS_VISIBLE,0,0,260,200,root,nullptr,GetModuleHandleW(nullptr),nullptr);if(!child)throw std::runtime_error("child-unavailable");
            winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget desktop{nullptr};auto interop=compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();winrt::check_hresult(interop->CreateDesktopWindowTarget(child,TRUE,(ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**)winrt::put_abi(desktop)));
            auto sprite=compositor.CreateSpriteVisual();sprite.Size({260,200});sprite.Brush(compositor.CreateColorBrush({0,0,0,0}));desktop.Root(sprite);
            auto controller=winrt::Windows::UI::Core::CoreIndependentInputSourceController::CreateForVisual(sprite);
            controller.SetControlledInput(policy==2?winrt::Windows::UI::Core::CoreInputDeviceTypes::Touch:winrt::Windows::UI::Core::CoreInputDeviceTypes::Touch|winrt::Windows::UI::Core::CoreInputDeviceTypes::Pen);controller.IsTransparentForUncontrolledInput(true);
            const bool defaultPalm=controller.IsPalmRejectionEnabled();if(policy)controller.IsPalmRejectionEnabled(false);auto source=controller.Source();unsigned downCount=0,upCount=0;
            source.PointerPressed([&](auto const&,auto const& args){++downCount;auto p=args.CurrentPoint();handlePointer(root,0,WM_POINTERDOWN,p.PointerId(),p.Position().X,p.Position().Y,0);args.Handled(true);});
            source.PointerMoved([&](auto const&,auto const& args){auto p=args.CurrentPoint();handlePointer(root,0,WM_POINTERUPDATE,p.PointerId(),p.Position().X,p.Position().Y,0);args.Handled(true);});
            source.PointerReleased([&](auto const&,auto const& args){++upCount;auto p=args.CurrentPoint();handlePointer(root,0,WM_POINTERUP,p.PointerId(),p.Position().X,p.Position().Y,0);args.Handled(true);});
            SetWindowLongPtrW(root,GWL_EXSTYLE,GetWindowLongPtrW(root,GWL_EXSTYLE)|WS_EX_LAYERED);if(!uploadEmptyInput(root))throw std::runtime_error("alpha-upload-unavailable");
            if(policy==3)source.SetPointerCapture();pump(100);const LONG mouseBefore=shared->leftDowns,wheelBefore=shared->wheels,touchBefore=shared->touchDowns;
            bool injected=mouseClickAndWheel(target,{450,350});injected=contactPair(target,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;unsigned held=0;
            for(int i=0;i<25;++i){Sleep(5);injected=contactPair(target,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(15);if(core.pads[0].down&&core.pads[1].down)++held;}
            const bool heldAtEnd=core.pads[0].down&&core.pads[1].down;injected=contactPair(target,POINTER_FLAG_UP)&&injected;pump(80);
            const bool row=injected&&heldAtEnd&&downCount==2&&upCount==2&&shared->leftDowns-mouseBefore==1&&shared->wheels-wheelBefore==1&&shared->touchDowns==touchBefore;
            result["cases"].push_back({{"policy",policy},{"defaultPalmRejectionEnabled",defaultPalm},{"actualPalmRejectionEnabled",controller.IsPalmRejectionEnabled()},{"captureRequested",policy==3},{"hasCapture",source.HasCapture()},
                {"inputOk",injected},{"heldSamples",held},{"heldAtEnd",heldAtEnd},{"downCount",downCount},{"upCount",upCount},{"targetMouseClicks",shared->leftDowns-mouseBefore},{"targetWheelEvents",shared->wheels-wheelBefore},{"targetTouchDowns",shared->touchDowns-touchBefore},{"stationaryJointAcceptance",row}});
            valid=valid&&injected;controller.Close();source=nullptr;desktop.Close();DestroyWindow(child);shutdown();pump(30);
        }compositor.Close();
    }catch(const std::exception& e){valid=false;result["failure"]=e.what();shutdown();}catch(const winrt::hresult_error& e){valid=false;result["failure"]=(long)e.code();shutdown();}
    result["ownedTargetTerminal"]=target.stop();valid=valid&&result["ownedTargetTerminal"].get<bool>();result["diagnosticValid"]=valid;POINT now{};GetCursorPos(&now);if(now.x==450&&now.y==350)SetCursorPos(oldCursor.x,oldCursor.y);if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<result.dump(2);std::cout<<"PALM_POLICY_PROBE valid="<<valid<<" cases="<<result["cases"].size()<<"\n";return valid&&file.good()?0:2;
}