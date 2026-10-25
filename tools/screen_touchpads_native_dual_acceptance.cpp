#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads_native_topology_test_host.h"
static bool pairFrame(const OwnTarget& target,const std::array<POINT,2>& points,DWORD flags) {
    POINTER_TOUCH_INFO touch[2]{};for(int side=0;side<2;++side){const auto point=points[side];if(!authorizedPoint(target,point))return false;
        auto& t=touch[side];t.pointerInfo.pointerType=PT_TOUCH;t.pointerInfo.pointerId=side;t.pointerInfo.pointerFlags=flags;t.pointerInfo.ptPixelLocation=point;
        t.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE|TOUCH_MASK_ORIENTATION;t.rcContact={point.x-3,point.y-3,point.x+3,point.y+3};t.pressure=512;t.orientation=90;}
    return InjectTouchInput(2,touch)!=FALSE;
}
static void placeOwnedPair(const OwnTarget& target) {
    SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);
    for(int side=0;side<2;++side){if(!windows[side])throw std::runtime_error("dual-pad-window-missing");const int x=325+side*230;
        SetWindowPos(windows[side],HWND_TOPMOST,x,270,210,210,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(outlines[side],HWND_TOPMOST,x,270,210,210,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[side]={210,210};
        if(nativeInputCandidateRequested&&!nativeInputSync(side,true,false))throw std::runtime_error("dual-component-unavailable");}
    pump(100);
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);if(argc!=2)return 2;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);POINT oldCursor{};GetCursorPos(&oldCursor);const auto foreground=GetForegroundWindow();OwnTarget target;bool valid=true,passed=true;POINT lastMouse{};
    Json trace=Json::array();testTrace=&trace;Json result={{"diagnosticValid",false},{"dualProductionComponentPassed",false},{"featureAcceptancePassed",false},{"systemOutputInjected",false},{"virtualTargetStarted",false},{"productDeployed",false},{"cases",Json::array()}};
    try {if(!target.start())throw std::runtime_error("owned-target-unavailable");if(!InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-injection-unavailable");winrt::init_apartment(winrt::apartment_type::single_threaded);
        for(int profile=0;profile<4;++profile) {
            setNativeInputCandidate(false);dryRun=true;setContext(profile==0?"steamdeck":profile==3?"dualsense-edge":"disabled",profile==0||profile==3);
            Config cfg=profileDefault(activeProfile.load());cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=profile==0?Mode::Deck:profile==1?Mode::Wasd:profile==2?Mode::Arrows:Mode::DualSense;
            cfg.right=profile==0?Mode::Deck:profile==3?Mode::DualSense:Mode::Mouse;initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();placeOwnedPair(target);
            std::array<POINT,2> point={{{385,330},{615,330}}};
            bool control=pairFrame(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);Sleep(3);for(auto& p:point){p.x+=64;p.y+=40;}control=pairFrame(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&control;pump(60);
            const bool controlHeld=core.pads[0].down&&core.pads[1].down;result["controlContexts"].push_back({{"profile",profile},{"activeProfile",activeProfile.load()},{"appliedProfile",appliedProfile.load()},{"enabled",enabled.load()},{"leftPermitted",permitted(cfg.left)},{"rightPermitted",permitted(cfg.right)},{"snapshot",snapshotJson(snapshot())}});control=pairFrame(target,point,POINTER_FLAG_UP)&&control;pump(60);
            control=control&&controlHeld&&!core.pads[0].down&&!core.pads[1].down;result["legacyPositiveControls"].push_back(control);valid=valid&&control;
            setNativeInputCandidate(true);placeOwnedPair(target);
            for(int round=0;round<6;++round) {
                point={{{385,330},{615,330}}};
                const LONG mouseBefore=shared->leftDowns,wheelBefore=shared->wheels,touchBefore=shared->touchDowns,touchUpBefore=shared->touchUps,touchMoveBefore=shared->touchUpdates;
                bool injected=pairFrame(target,point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
                lastMouse=point[round%2];const bool mouseInjected=mouseClickAndWheel(target,lastMouse);Sleep(3);for(auto& p:point){p.x+=64;p.y+=40;}injected=pairFrame(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(60);
                const bool held=core.pads[0].down&&core.pads[1].down;bool retained=held;
                if(round==0){const auto end=GetTickCount64()+500;while(GetTickCount64()<end){Sleep(20);injected=pairFrame(target,point,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(5);retained=retained&&core.pads[0].down&&core.pads[1].down;}}
                if(round==1){cfg.transparency=100;const bool reset=configure(cfg);retained=retained&&!reset&&core.pads[0].down&&core.pads[1].down;}
                if(round==2){windowProc(windows[0],WM_CANCELMODE,0,0);retained=retained&&!core.pads[0].down&&!core.pads[1].down&&!sentKeys;}
                if(round==3){const auto old=core.pads[0].id;handleCaptureLoss(windows[0],0,old);retained=retained&&!core.pads[0].down&&core.pads[1].down;}
                if(round==4){Config off=cfg;off.enabled=false;configure(off);retained=retained&&!core.pads[0].down&&!core.pads[1].down&&!sentKeys;}
                injected=pairFrame(target,point,POINTER_FLAG_UP)&&injected;pump(60);
                const bool released=!core.pads[0].down&&!core.pads[1].down&&!sentKeys&&!sentClick;
                const bool row=injected&&mouseInjected&&held&&retained&&released&&shared->leftDowns-mouseBefore==1&&shared->wheels-wheelBefore==1&&shared->touchDowns==touchBefore&&shared->touchUps==touchUpBefore&&shared->touchUpdates==touchMoveBefore;
                valid=valid&&injected&&mouseInjected;passed=passed&&row;
                result["cases"].push_back({{"profile",profile},{"left",modeName(cfg.left)},{"right",modeName(cfg.right)},{"round",round},{"motionEstablishedBeforeHold",true},{"inputOk",injected&&mouseInjected},{"bothHeld",held},{"expectedLifecycleState",retained},{"released",released},
                    {"targetMouseClicks",shared->leftDowns-mouseBefore},{"targetWheelEvents",shared->wheels-wheelBefore},{"rawTouchDownsBelow",shared->touchDowns-touchBefore},{"rawTouchUpsBelow",shared->touchUps-touchUpBefore},{"rawTouchMovesBelow",shared->touchUpdates-touchMoveBefore},{"passed",row}});
                if(round==4){configure(cfg);placeOwnedPair(target);}
            }
            const std::array<HWND,2> children={nativeInputSurfaces[0].child(),nativeInputSurfaces[1].child()};shutdown();const bool closed=!IsWindow(children[0])&&!IsWindow(children[1]);passed=passed&&closed;result["closedChecks"].push_back(closed);setNativeInputCandidate(false);pump(30);
        }
    }catch(const std::exception& e){valid=false;passed=false;result["failure"]=e.what();shutdown();}catch(const winrt::hresult_error& e){valid=false;passed=false;result["failure"]=(long)e.code();shutdown();}
    result["trace"]=trace;testTrace=nullptr;result["ownedTargetTerminal"]=target.stop();valid=valid&&result["ownedTargetTerminal"].get<bool>();result["diagnosticValid"]=valid;result["dualProductionComponentPassed"]=valid&&passed;
    POINT now{};GetCursorPos(&now);if(now.x==lastMouse.x&&now.y==lastMouse.y)SetCursorPos(oldCursor.x,oldCursor.y);if(foreground&&IsWindow(foreground))SetForegroundWindow(foreground);
    std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<result.dump(2);std::cout<<"DUAL_COMPONENT valid="<<valid<<" passed="<<(valid&&passed)<<" cases="<<result["cases"].size()<<"\n";return valid&&passed&&file.good()?0:2;
}