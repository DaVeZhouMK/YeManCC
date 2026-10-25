#include "screen_touchpads_native_topology_test_host.h"
struct Topology {
    const char* name;
    int region; // 0=full, 1=empty, 2=corner, 3=hollow border
    bool child;
    int parentStyle; // 0=normal, 1=layered-transparent, 2=per-pixel-zero
    bool independent;
    bool topmostVisual;
};
static bool applyRegion(HWND h,int kind) {
    if(!kind)return true;
    HRGN region=CreateRectRgn(0,0,kind==1?0:(kind==2?1:260),kind==1?0:(kind==2?1:200));
    if(!region)return false;
    if(kind==3) {
        HRGN interior=CreateRectRgn(2,2,258,198);
        if(!interior){DeleteObject(region);return false;}
        const int combined=CombineRgn(region,region,interior,RGN_DIFF);DeleteObject(interior);
        if(combined==ERROR){DeleteObject(region);return false;}
    }
    if(SetWindowRgn(h,region,TRUE))return true; // Windows owns it only on success.
    DeleteObject(region);return false;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);
    if(argc<2||argc>3)return 2;
    const bool requirePass=argc==3&&std::wstring(argv[2])==L"--require-pass";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT oldCursor{};GetCursorPos(&oldCursor);const HWND oldForeground=GetForegroundWindow();
    Json result={{"goal","mouse passes cross-process while touch stays captured"},{"featureAcceptancePassed",false},
        {"diagnosticValid",false},{"productionCodeChanged",false},{"productDeployed",false},{"configurationWrites",0},
        {"virtualTargetStarted",false},{"testMouseInjected",true},{"testTouchInjected",true},{"touchpadSystemOutputInjected",false},
        {"clickForwarded",false},{"globalHookInstalled",false},{"uiAccessChanged",false},{"foreignWindowReparented",false},{"cases",Json::array()}};
    OwnTarget target;bool valid=true;unsigned passingSamples=0;
    try {
        if(!target.start())throw std::runtime_error("own-target-not-ready");
        result["childPid"]=target.child.dwProcessId;result["ownedTargetForegroundVerified"]=true;
        if(!InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-injection-unavailable");
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Windows::System::DispatcherQueueController queue{nullptr};
        DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
        winrt::check_hresult(CreateDispatcherQueueController(options,(ABI::Windows::System::IDispatcherQueueController**)winrt::put_abi(queue)));
        winrt::Windows::UI::Composition::Compositor compositor;
        const std::array<Topology,11> cases={{{"production-control",0,false,0,false,true},
            {"controlled-touch-positive",0,false,0,true,true},
            {"controlled-empty-region",1,false,0,true,true},
            {"controlled-corner-region",2,false,0,true,true},
            {"controlled-hollow-region",3,false,0,true,true},
            {"owned-child-positive",0,true,0,true,true},
            {"owned-child-layered-transparent-parent",0,true,1,true,true},
            {"owned-child-zero-alpha-parent",0,true,2,true,true},
            {"owned-child-empty-parent-region",1,true,0,true,true},
            {"owned-child-hollow-parent-region",3,true,0,true,true},
            {"controlled-empty-region-nontopmost-visual",1,false,0,true,false}}};
        for(const auto& test:cases) {
            Config cfg=profileDefault(0);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;
            dryRun=true;setContext("disabled",false);initialize(GetModuleHandleW(nullptr),nullptr,cfg,0);refresh();
            const HWND input=windows[0];if(!input)throw std::runtime_error("production-input-window-missing");
            SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(input,HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]={260,200};
            HWND child=nullptr;HWND sourceWindow=input;bool setupOk=true;Json apiErrors=Json::array();
            if(test.child) {
                child=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"YeManScreenTouchpad",L"YMCC owned topology child",WS_CHILD|WS_VISIBLE,
                    0,0,260,200,input,nullptr,GetModuleHandleW(nullptr),nullptr);
                if(!child)setupOk=false;else sourceWindow=child;
                DWORD rootPid=0,childPid=0;GetWindowThreadProcessId(input,&rootPid);GetWindowThreadProcessId(child,&childPid);
                setupOk=setupOk&&rootPid==GetCurrentProcessId()&&childPid==GetCurrentProcessId()&&GetParent(child)==input;
            }
            winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget desktop{nullptr};
            winrt::Windows::UI::Core::CoreIndependentInputSourceController controller{nullptr};
            winrt::Windows::UI::Core::CoreIndependentInputSource source{nullptr};
            unsigned sourceDowns=0,sourceUps=0;Json pointerDetails=Json::array();
            if(test.independent&&setupOk) {
                try {
                    auto interop=compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
                    winrt::check_hresult(interop->CreateDesktopWindowTarget(sourceWindow,test.topmostVisual,
                        (ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**)winrt::put_abi(desktop)));
                    auto sprite=compositor.CreateSpriteVisual();sprite.Size({260,200});sprite.Brush(compositor.CreateColorBrush({0,0,0,0}));desktop.Root(sprite);
                    controller=winrt::Windows::UI::Core::CoreIndependentInputSourceController::CreateForVisual(sprite);
                    controller.SetControlledInput(winrt::Windows::UI::Core::CoreInputDeviceTypes::Touch|winrt::Windows::UI::Core::CoreInputDeviceTypes::Pen);
                    controller.IsTransparentForUncontrolledInput(true);source=controller.Source();inputDispatcher=source.Dispatcher();
                    source.PointerPressed([&](auto const&,auto const& args){++sourceDowns;auto p=args.CurrentPoint();pointerDetails.push_back({{"id",p.PointerId()},{"phase","down"}});args.Handled(true);});
                    source.PointerReleased([&](auto const&,auto const& args){++sourceUps;args.Handled(true);});
                }catch(const winrt::hresult_error& error){setupOk=false;apiErrors.push_back((long)error.code());}
            }
            const auto base=GetWindowLongPtrW(input,GWL_EXSTYLE);
            if(test.parentStyle==1){SetWindowLongPtrW(input,GWL_EXSTYLE,base|WS_EX_LAYERED|WS_EX_TRANSPARENT);setupOk=SetLayeredWindowAttributes(input,0,255,LWA_ALPHA)!=FALSE&&setupOk;}
            if(test.parentStyle==2){SetWindowLongPtrW(input,GWL_EXSTYLE,base|WS_EX_LAYERED);setupOk=uploadEmptyInput(input)&&setupOk;}
            setupOk=applyRegion(input,test.region)&&setupOk;
            SetWindowPos(input,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);pump(100);
            const POINT point{450,350};
            for(int burst=0;burst<2;burst++) {
                const LONG mouseBefore=shared->leftDowns,touchBefore=shared->touchDowns,wheelBefore=shared->wheels;
                const auto eventsBefore=eventCount;const auto downBefore=sourceDowns,upBefore=sourceUps;
                const bool mouseSent=mouseClickAndWheel(target,point);if(!burst)pump(80);
                SetLastError(0);const bool down=injectContact(target,point,true);const DWORD downError=GetLastError();pump(60);
                const bool up=injectContact(target,point,false);const DWORD upError=GetLastError();pump(90);
                const LONG mouse=shared->leftDowns-mouseBefore,touches=shared->touchDowns-touchBefore,wheel=shared->wheels-wheelBefore;
                const auto events=eventCount-eventsBefore;const auto pressed=sourceDowns-downBefore,released=sourceUps-upBefore;
                const bool touchCaptured=test.independent?(pressed==1&&released==1):(events>=2);
                const bool both=setupOk&&mouseSent&&down&&up&&mouse==1&&wheel==1&&touchCaptured&&touches==0;
                const bool isControl=std::string(test.name)=="production-control"||std::string(test.name)=="controlled-touch-positive"||std::string(test.name)=="owned-child-positive";
                const bool controlValid=!isControl||(setupOk&&mouse==0&&touchCaptured&&touches==0);
                valid=valid&&controlValid&&setupOk&&mouseSent&&down&&up;
                if(!isControl&&both)++passingSamples;
                result["cases"].push_back({{"name",test.name},{"freshInputWindow",true},{"ownedChild",test.child},{"region",test.region},
                    {"overlappedBurst",burst!=0},{"setupOk",setupOk},{"apiErrors",apiErrors},{"mouseInjected",mouseSent},{"touchDownInjected",down},
                    {"touchUpInjected",up},{"downError",downError},{"upError",upError},{"targetMouseClicks",mouse},{"targetWheelEvents",wheel},
                    {"targetTouchDowns",touches},{"padPointerEvents",events},{"independentSourceDowns",pressed},{"independentSourceUps",released},
                    {"touchCaptured",touchCaptured},{"mouseAndTouchAcceptance",both},{"isPositiveControl",isControl},{"positiveControlValid",controlValid}});
            }
            inputDispatcher=nullptr;if(controller)controller.Close();source=nullptr;if(desktop)desktop.Close();
            if(child)DestroyWindow(child);shutdown();pump(30);
        }
        compositor.Close();result["candidateFound"]=passingSamples!=0;result["passingSamples"]=passingSamples;
        // Diagnostic hypotheses do not replace the fail-closed production adapter gate.
        result["featureAcceptancePassed"]=false;
    }catch(const std::exception& error){result["failure"]=error.what();valid=false;shutdown();}
     catch(const winrt::hresult_error& error){result["failure"]=(long)error.code();valid=false;shutdown();}
    result["ownedTargetTerminal"]=target.stop();valid=valid&&result["ownedTargetTerminal"].get<bool>();result["diagnosticValid"]=valid;
    POINT now{};GetCursorPos(&now);if(now.x==450&&now.y==350)SetCursorPos(oldCursor.x,oldCursor.y);
    if(oldForeground&&IsWindow(oldForeground))SetForegroundWindow(oldForeground);
    std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<result.dump(2);
    std::cout<<"NATIVE_TOPOLOGY_PROBE diagnosticValid="<<valid<<" passingSamples="<<passingSamples<<"\n";
    if(!valid||!out.good())return 2;
    return requirePass&&!result["featureAcceptancePassed"].get<bool>()?3:0;
}