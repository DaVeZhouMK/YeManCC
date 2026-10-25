#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
static bool contact(const OwnTarget& target,POINT point,DWORD flags){if(!target.owns(GetForegroundWindow())||!authorizedPoint(target,point))return false;
    POINTER_TOUCH_INFO info{};info.pointerInfo.pointerType=PT_TOUCH;info.pointerInfo.pointerId=0;info.pointerInfo.pointerFlags=flags;info.pointerInfo.ptPixelLocation=point;
    info.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE|TOUCH_MASK_ORIENTATION;info.rcContact={point.x-3,point.y-3,point.x+3,point.y+3};info.pressure=512;info.orientation=90;return InjectTouchInput(1,&info)!=FALSE;}
int wmain(int argc,wchar_t** argv){if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc<2||argc>3)return 2;const bool commit=argc==3&&std::wstring(argv[2])==L"--commit",dispatch=argc==3&&std::wstring(argv[2])==L"--dispatch";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);POINT oldCursor{};GetCursorPos(&oldCursor);const auto foreground=GetForegroundWindow();OwnTarget target;bool valid=true,passed=true;
    Json report={{"explicitCommitRequested",commit},{"explicitDispatcherProcessed",dispatch},{"diagnosticValid",false},{"coldFirstContactPassed",false},{"featureAcceptancePassed",false},{"systemOutputInjected",false},{"virtualTargetStarted",false},{"productDeployed",false},{"cases",Json::array()}};
    try{if(!target.start())throw std::runtime_error("owned-target-unavailable");if(!InitializeTouchInjection(1,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-unavailable");winrt::init_apartment(winrt::apartment_type::single_threaded);
        for(bool button:{false,true})for(DWORD settle:{0u,16u,100u})for(int round=0;round<4;++round){
            setNativeInputCandidate(true);dryRun=true;setContext("disabled",false);Config cfg=profileDefault(0);cfg.enabled=!button;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;cfg.specialMask=cfg.rearMask=0;cfg.specialEnabled=cfg.rearEnabled=false;cfg.summonPosition=button?SummonPosition::Right:SummonPosition::Off;
            initialize(GetModuleHandleW(nullptr),nullptr,cfg);HWND root=button?buttonWindows[0]:windows[0];if(!root)throw std::runtime_error("input-root-unavailable");
            SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(root,HWND_TOPMOST,360,280,button?100:260,button?80:200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            HWND outline=button?buttonOutlines[0]:outlines[0];SetWindowPos(outline,HWND_TOPMOST,360,280,button?100:260,button?80:200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            if(button){if(!buttonNativeInputSync(0,true,false))throw std::runtime_error("button-sync-failed");}else{sizes[0]=sizes[1]={260,200};if(!nativeInputSync(0,true,false))throw std::runtime_error("pad-sync-failed");}
            auto source=button?nativeButtonSurfaces[0].source():nativeInputSurfaces[0].source();const bool enabledBeforePump=source.IsInputEnabled();
            unsigned inputEnabledEdges=0;auto readyToken=source.InputEnabled([&](auto const&,auto const&){++inputEnabledEdges;});
            bool commitCompleted=false;ULONGLONG readyWaitMs=0;
            if(commit){const auto began=GetTickCount64();auto action=nativeInputRuntime.compositor.RequestCommitAsync();while(action.Status()==winrt::Windows::Foundation::AsyncStatus::Started&&GetTickCount64()-began<1000)pump(1);
                commitCompleted=action.Status()==winrt::Windows::Foundation::AsyncStatus::Completed;readyWaitMs=GetTickCount64()-began;if(!commitCompleted)throw std::runtime_error("composition-commit-not-complete");}
            if(dispatch)source.Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);
            if(settle)pump(settle);const bool enabledBeforeDown=source.IsInputEnabled();const LONG mouseBefore=shared->leftDowns,wheelBefore=shared->wheels,touchBefore=shared->touchDowns,touchUpBefore=shared->touchUps,touchMoveBefore=shared->touchUpdates;
            const auto eventsBefore=button?buttonEventCount:eventCount;POINT point{400,310};bool injected=mouseClickAndWheel(target,point);injected=contact(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;
            Sleep(3);point.x+=15;point.y+=10;injected=contact(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(40);
            const bool held=button?buttonContacts[0]!=0:core.pads[0].down;injected=contact(target,point,POINTER_FLAG_UP)&&injected;pump(80);
            const auto events=(button?buttonEventCount:eventCount)-eventsBefore;const bool released=button?buttonContacts[0]==0:!core.pads[0].down;
            const bool mouse=shared->leftDowns-mouseBefore==1&&shared->wheels-wheelBefore==1;const bool noLeak=shared->touchDowns==touchBefore&&shared->touchUps==touchUpBefore&&shared->touchUpdates==touchMoveBefore;
            const bool row=injected&&mouse&&noLeak&&events==3&&released;valid=valid&&injected;passed=passed&&row;
            report["cases"].push_back({{"role",button?"button":"pad"},{"settleMs",settle},{"round",round},{"freshInputWindows",true},{"sourceEnabledBeforePump",enabledBeforePump},{"sourceEnabledBeforeDown",enabledBeforeDown},{"inputEnabledEvents",inputEnabledEdges},{"commitCompleted",commitCompleted},{"readyWaitMs",readyWaitMs},{"inputOk",injected},{"mousePassed",mouse},{"rawTouchLeaked",!noLeak},{"events",events},{"heldBeforeUp",held},{"released",released},{"passed",row}});
            source.InputEnabled(readyToken);source=nullptr;shutdown();setNativeInputCandidate(false);pump(10);
        }
    }catch(const std::exception& e){valid=false;passed=false;report["failure"]=e.what();shutdown();}catch(const winrt::hresult_error& e){valid=false;passed=false;report["failure"]=(long)e.code();shutdown();}
    report["ownedTargetTerminal"]=target.stop();valid=valid&&report["ownedTargetTerminal"].get<bool>();report["diagnosticValid"]=valid;report["coldFirstContactPassed"]=valid&&passed;
    POINT now{};GetCursorPos(&now);if(now.x==400&&now.y==310)SetCursorPos(oldCursor.x,oldCursor.y);if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<report.dump(2);std::cout<<"COLD_FIRST_CONTACT valid="<<valid<<" passed="<<(valid&&passed)<<" cases="<<report["cases"].size()<<"\n";return valid&&passed&&file.good()?0:2;
}