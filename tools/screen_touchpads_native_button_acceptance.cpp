#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
static unsigned actionCalls=0,summonMessages=0;static StandaloneAction lastAction=StandaloneAction::None;
static LRESULT CALLBACK summonSink(HWND h,UINT m,WPARAM w,LPARAM l){if(m==kSummonMessage){++summonMessages;return 0;}return DefWindowProcW(h,m,w,l);}
static bool touch(const OwnTarget& target,POINT point,DWORD flags){if(!target.owns(GetForegroundWindow())||!authorizedPoint(target,point))return false;
    POINTER_TOUCH_INFO info{};info.pointerInfo.pointerType=PT_TOUCH;info.pointerInfo.pointerId=0;info.pointerInfo.pointerFlags=flags;info.pointerInfo.ptPixelLocation=point;
    info.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE|TOUCH_MASK_ORIENTATION;info.rcContact={point.x-3,point.y-3,point.x+3,point.y+3};info.pressure=512;info.orientation=90;return InjectTouchInput(1,&info)!=FALSE;}
int wmain(int argc,wchar_t** argv){if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);POINT oldCursor{};GetCursorPos(&oldCursor);const HWND foreground=GetForegroundWindow();OwnTarget target;bool valid=true,passed=true;
    Json report={{"diagnosticValid",false},{"buttonComponentPassed",false},{"featureAcceptancePassed",false},{"systemOutputInjected",false},{"virtualTargetStarted",false},{"productDeployed",false},{"steamOrMicrophoneActionsExecuted",false},{"clickForwarded",false},{"cases",Json::array()}};
    try{if(!target.start())throw std::runtime_error("owned-target-unavailable");if(!InitializeTouchInjection(1,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-unavailable");winrt::init_apartment(winrt::apartment_type::single_threaded);
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YmccOwnButtonSummonSink";cls.lpfnWndProc=summonSink;RegisterClassW(&cls);
        buttonSummonTestOwner=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);if(!buttonSummonTestOwner)throw std::runtime_error("summon-sink-unavailable");
        standaloneSubmit=[](StandaloneAction action,UINT_PTR generation){++actionCalls;lastAction=action;standaloneFinish(generation,StandaloneStatus::Accepted);return true;};
        for(int persona=0;persona<5;++persona)for(int index=0;index<7;++index)for(int stop=0;stop<4;++stop){
            setNativeInputCandidate(true);dryRun=true;setContext(persona==0?"steamdeck":persona==1?"dualsense-edge":persona==2?"elite":"disabled",persona<3);
            Config cfg=profileDefault(activeProfile.load());cfg.enabled=false;cfg.summonPosition=SummonPosition::Right;cfg.specialMask=3;cfg.rearMask=15;cfg.specialEnabled=cfg.rearEnabled=false;
            if(persona>=3)cfg.standaloneSpecialMode=persona==3?StandaloneSpecialMode::SteamDeck:StandaloneSpecialMode::PS5;
            if(!buttonPermitted(cfg,index))continue;
            initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();HWND root=buttonWindows[index];if(!root||!nativeButtonSurfaces[index].active())throw std::runtime_error("production-button-source-unavailable");
            // Only the selected, authenticated own input rectangle is exercised.
            for(int other=0;other<7;++other)if(other!=index){if(buttonWindows[other])ShowWindow(buttonWindows[other],SW_HIDE);if(buttonOutlines[other])ShowWindow(buttonOutlines[other],SW_HIDE);}
            SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(root,HWND_TOPMOST,360,280,80,60,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(buttonOutlines[index],HWND_TOPMOST,360,280,80,60,SWP_NOACTIVATE|SWP_SHOWWINDOW);if(!buttonNativeInputSync(index,true,false))throw std::runtime_error("button-size-sync-failed");pump(100);
            const bool active=state().value("nativeMousePassActive",false);const LONG mouseBefore=shared->leftDowns,wheelBefore=shared->wheels,downBefore=shared->touchDowns,upBefore=shared->touchUps,moveBefore=shared->touchUpdates;
            const auto eventsBefore=buttonEventCount,actionBefore=actionCalls,summonBefore=summonMessages;const UINT_PTR pendingBefore=standalonePending.load();
            POINT point{400,310};bool injected=mouseClickAndWheel(target,point);injected=touch(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;
            Sleep(3);point.x+=20;point.y+=14;injected=touch(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(40);
            const auto downDeadline=GetTickCount64()+300;while(!buttonContacts[index]&&GetTickCount64()<downDeadline)pump(5);
            const bool held=buttonContacts[index]!=0&&buttonArmed[index];const unsigned bit=index?1u<<(index-1):0u;const bool expectedMask=buttonSnapshot()==(persona<3?bit:0u);
            if(stop==1){nativeButtonSurfaces[index].cancel();}
            if(stop==2){if(persona>=3)cfg.standaloneSpecialMode=StandaloneSpecialMode::Off;else if(index==0)cfg.summonPosition=SummonPosition::Off;else if(index<3)cfg.specialMask=cfg.specialEnabled=0;else cfg.rearMask=cfg.rearEnabled=0;configure(cfg);}
            const HWND child=nativeButtonSurfaces[index].child();if(stop==3)shutdown();
            injected=touch(target,point,POINTER_FLAG_UP)&&injected;pump(50);
            const bool released=!buttonContacts[index]&&!buttonArmed[index]&&!buttonSnapshot()&&standalonePending.load()==pendingBefore;
            const unsigned expectedAction=stop==0&&persona>=3&&index>0?1u:0u,expectedSummon=stop==0&&index==0?1u:0u;
            const bool action=actionCalls-actionBefore==expectedAction&&(!expectedAction||lastAction==standaloneAction(persona==3?StandaloneSpecialMode::SteamDeck:StandaloneSpecialMode::PS5,index));
            const bool summon=summonMessages-summonBefore==expectedSummon;
            const bool noLeaks=shared->touchDowns==downBefore&&shared->touchUps==upBefore&&shared->touchUpdates==moveBefore;
            const bool mouse=shared->leftDowns-mouseBefore==1&&shared->wheels-wheelBefore==1;const bool noDuplicates=buttonEventCount-eventsBefore==(stop?2u:3u);
            const bool row=injected&&active&&held&&expectedMask&&released&&action&&summon&&noLeaks&&mouse&&noDuplicates;
            valid=valid&&injected;passed=passed&&row;report["cases"].push_back({{"persona",persona},{"button",index},{"stop",stop},{"inputOk",injected},{"nativeStateActive",active},{"held",held},{"personaMaskCorrect",expectedMask},{"released",released},
                {"actionCountCorrect",action},{"summonCountCorrect",summon},{"originalEvents",buttonEventCount-eventsBefore},{"noDuplicateDelivery",noDuplicates},{"mousePassed",mouse},{"rawTouchLeaked",!noLeaks},{"passed",row}});
            shutdown();const bool closed=!IsWindow(child)&&!nativeButtonSurfaces[index].root();passed=passed&&closed;report["closed"].push_back(closed);setNativeInputCandidate(false);pump(10);
        }
    }catch(const std::exception& e){valid=false;passed=false;report["failure"]=e.what();shutdown();}catch(const winrt::hresult_error& e){valid=false;passed=false;report["failure"]=(long)e.code();shutdown();}
    standaloneSubmit=nullptr;if(buttonSummonTestOwner)DestroyWindow(buttonSummonTestOwner);buttonSummonTestOwner=nullptr;report["ownedTargetTerminal"]=target.stop();valid=valid&&report["ownedTargetTerminal"].get<bool>();report["diagnosticValid"]=valid;report["buttonComponentPassed"]=valid&&passed;
    POINT now{};GetCursorPos(&now);if(now.x==400&&now.y==310)SetCursorPos(oldCursor.x,oldCursor.y);if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<report.dump(2);std::cout<<"BUTTON_INPUT_COMPONENT valid="<<valid<<" passed="<<(valid&&passed)<<" cases="<<report["cases"].size()<<"\n";return valid&&passed&&file.good()?0:2;
}