// Candidate stress gate: real cross-process routing, no output replay or installed-product changes.
#include "screen_touchpads_native_topology_test_host.h"
#include <set>
#include <tpcshrd.h>
static bool mousePart(const OwnTarget& target,POINT point,DWORD button=0,bool wheel=false) {
    if(!authorizedPoint(target,point)||(wheel&&!target.owns(GetForegroundWindow())))return false;
    INPUT input{};input.type=INPUT_MOUSE;input.mi.dwExtraInfo=0x594d5047u;
    const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    input.mi.dx=MulDiv(point.x-x,65535,width-1);input.mi.dy=MulDiv(point.y-y,65535,height-1);
    input.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|MOUSEEVENTF_MOVE_NOCOALESCE|button;
    if(wheel){input.mi.dwFlags|=MOUSEEVENTF_WHEEL;input.mi.mouseData=WHEEL_DELTA;}
    return SendInput(1,&input,sizeof(input))==1;
}
static bool touchFrame(const OwnTarget& target,POINT point,unsigned count,DWORD flags) {
    if(count<1||count>2||!authorizedPoint(target,point))return false;
    POINTER_TOUCH_INFO frame[2]{};
    for(unsigned i=0;i<count;++i){auto& c=frame[i];c.pointerInfo.pointerType=PT_TOUCH;c.pointerInfo.pointerId=i;
        c.pointerInfo.ptPixelLocation={point.x+(LONG)i*30,point.y+(LONG)i*20};
        if(!authorizedPoint(target,c.pointerInfo.ptPixelLocation))return false;
        c.pointerInfo.pointerFlags=flags;c.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE|TOUCH_MASK_ORIENTATION;c.orientation=90;
        const auto p=c.pointerInfo.ptPixelLocation;c.rcContact={p.x-3,p.y-3,p.x+3,p.y+3};c.pressure=512;}
    return InjectTouchInput(count,frame)!=FALSE;
}
static bool penFrame(const OwnTarget& target,HSYNTHETICPOINTERDEVICE device,POINT point,DWORD flags) {
    if(!device||!authorizedPoint(target,point))return false;
    POINTER_TYPE_INFO frame{};frame.type=PT_PEN;frame.penInfo.pointerInfo.pointerType=PT_PEN;
    frame.penInfo.pointerInfo.pointerId=0;frame.penInfo.pointerInfo.ptPixelLocation=point;frame.penInfo.pointerInfo.pointerFlags=flags;
    frame.penInfo.penMask=PEN_MASK_PRESSURE;frame.penInfo.pressure=512;
    return InjectSyntheticPointerInput(device,&frame,1)!=FALSE;
}
static LRESULT CALLBACK rawGestureProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_TABLET_QUERYSYSTEMGESTURESTATUS)return TABLET_DISABLE_PRESSANDHOLD|TABLET_DISABLE_FLICKS|TABLET_DISABLE_PENTAPFEEDBACK|TABLET_DISABLE_PENBARRELFEEDBACK;
    return windowProc(h,m,w,l);
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);
    if(argc<2||argc>4)return 2;const bool adaptCore=argc>=3&&std::wstring(argv[2])==L"--adapt-core";
    const bool vanillaNative=(argc>=3&&std::wstring(argv[2])==L"--vanilla-native")||(argc==4&&std::wstring(argv[3])==L"--vanilla-native");
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT oldCursor{};GetCursorPos(&oldCursor);const HWND oldForeground=GetForegroundWindow();
    Json result={{"diagnosticValid",false},{"candidateStressPassed",false},{"featureAcceptancePassed",false},{"productionCodeChanged",false},
        {"productDeployed",false},{"configurationWrites",0},{"clickForwarded",false},{"globalHookInstalled",false},{"uiAccessChanged",false},
        {"foreignWindowReparented",false},{"testMouseInjected",true},{"testTouchInjected",true},{"testPenInjected",true},
        {"touchpadSystemOutputInjected",false},{"cases",Json::array()}};
    result["productionCoreAdapterExercised"]=adaptCore;result["gestureSuppressionRequested"]=!vanillaNative;result["explicitCoreDispatcherPump"]=!vanillaNative;
    OwnTarget target;bool valid=true,passed=true;HSYNTHETICPOINTERDEVICE pen=nullptr;POINT finalCursor{};
    try {
        if(!target.start())throw std::runtime_error("own-target-not-ready");
        result["childPid"]=target.child.dwProcessId;result["ownedTargetForegroundVerified"]=true;
        if(!InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-injection-unavailable");

        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Windows::System::DispatcherQueueController queue{nullptr};DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
        winrt::check_hresult(CreateDispatcherQueueController(options,(ABI::Windows::System::IDispatcherQueueController**)winrt::put_abi(queue)));
        winrt::Windows::UI::Composition::Compositor compositor;result["digitizerFlags"]=GetSystemMetrics(SM_DIGITIZER);result["maximumTouches"]=GetSystemMetrics(SM_MAXIMUMTOUCHES);
        // Two fresh HWND lifecycles per device rule out stale hit-test/style caches.
        for(unsigned lifecycle=0;lifecycle<4;++lifecycle) {
            if(lifecycle>=2&&!pen){pen=CreateSyntheticPointerDevice(PT_PEN,1,POINTER_FEEDBACK_NONE);if(!pen)throw std::runtime_error("pen-injection-unavailable");}
            Config cfg=profileDefault(0);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=adaptCore&&(lifecycle%2)?Mode::DualSense:Mode::Mouse;dryRun=true;
            setContext(cfg.single==Mode::DualSense?"dualsense-edge":"disabled",cfg.single==Mode::DualSense);initialize(GetModuleHandleW(nullptr),nullptr,cfg);refresh();
            HWND root=windows[0];if(!root)throw std::runtime_error("input-root-missing");
            SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(root,HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            sizes[0]=sizes[1]={260,200};pump(30);const POINT win32Point{430,340};const auto win32Before=eventCount;
            bool win32Input=lifecycle<2?touchFrame(target,win32Point,1,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT):penFrame(target,pen,win32Point,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
            if(lifecycle<2){Sleep(3);win32Input=touchFrame(target,win32Point,1,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&win32Input;}pump(40);
            const auto win32DownDeadline=GetTickCount64()+300;while(!core.pads[0].down&&GetTickCount64()<win32DownDeadline)pump(5);
            const bool win32Held=core.pads[0].down;win32Input=(lifecycle<2?touchFrame(target,win32Point,1,POINTER_FLAG_UP):penFrame(target,pen,win32Point,POINTER_FLAG_UP))&&win32Input;pump(40);
            const auto win32UpDeadline=GetTickCount64()+300;while(core.pads[0].down&&GetTickCount64()<win32UpDeadline)pump(5);
            const bool win32Control=win32Input&&win32Held&&eventCount-win32Before>=2&&!core.pads[0].down;valid=valid&&win32Control;
            result["win32StationaryControls"].push_back({{"lifecycle",lifecycle},{"inputOk",win32Input},{"held",win32Held},{"passed",win32Control}});
            if(lifecycle<2&&cfg.single==Mode::DualSense) {
                const auto before=eventCount;bool injected=touchFrame(target,win32Point,2,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
                Sleep(3);injected=touchFrame(target,win32Point,2,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(80);
                const bool held=core.pads[0].down&&core.pads[1].down;
                injected=touchFrame(target,win32Point,2,POINTER_FLAG_UP)&&injected;pump(80);
                result["win32StationaryMultiTouchControl"]={{"inputOk",injected},{"bothHeldBeforeUp",held},{"events",eventCount-before}};
                result["stationaryMultiTouchPositiveControlValid"]=injected&&held;
                // This extra control diagnoses synthetic two-finger coalescing. It
                // does not erase passing motion samples or turn an unresolved
                // stationary candidate test into a production feature PASS.
                valid=valid&&injected;
            }
            const HWND child=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"YeManScreenTouchpad",L"YMCC controlled input child",WS_CHILD|WS_VISIBLE,
                0,0,260,200,root,nullptr,GetModuleHandleW(nullptr),nullptr);
            if(!child)throw std::runtime_error("owned-child-missing");
            if(!vanillaNative){
            const DWORD gestureFlags=TABLET_DISABLE_PRESSANDHOLD|TABLET_DISABLE_FLICKS|TABLET_DISABLE_PENTAPFEEDBACK|TABLET_DISABLE_PENBARRELFEEDBACK;
            SetWindowLongPtrW(root,GWLP_WNDPROC,(LONG_PTR)rawGestureProc);SetWindowLongPtrW(child,GWLP_WNDPROC,(LONG_PTR)rawGestureProc);
            const bool parentProperty=SetPropW(root,L"MicrosoftTabletPenServiceProperty",(HANDLE)(ULONG_PTR)gestureFlags)!=FALSE;
            const bool childProperty=SetPropW(child,L"MicrosoftTabletPenServiceProperty",(HANDLE)(ULONG_PTR)gestureFlags)!=FALSE;
            GESTURECONFIG gestures{0,0,GC_ALLGESTURES};const bool parentGesture=SetGestureConfig(root,0,1,&gestures,sizeof(gestures))!=FALSE;
            const bool childGesture=SetGestureConfig(child,0,1,&gestures,sizeof(gestures))!=FALSE;
            result["gestureSuppression"].push_back({{"lifecycle",lifecycle},{"parentProperty",parentProperty},{"childProperty",childProperty},{"parentGesture",parentGesture},{"childGesture",childGesture}});
            }
            winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget desktop{nullptr};
            auto interop=compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
            winrt::check_hresult(interop->CreateDesktopWindowTarget(child,TRUE,(ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**)winrt::put_abi(desktop)));
            auto sprite=compositor.CreateSpriteVisual();sprite.Size({260,200});sprite.Brush(compositor.CreateColorBrush({0,0,0,0}));desktop.Root(sprite);
            auto controller=winrt::Windows::UI::Core::CoreIndependentInputSourceController::CreateForVisual(sprite);
            controller.SetControlledInput(winrt::Windows::UI::Core::CoreInputDeviceTypes::Touch|winrt::Windows::UI::Core::CoreInputDeviceTypes::Pen);
            controller.IsTransparentForUncontrolledInput(true);auto source=controller.Source();if(!vanillaNative)inputDispatcher=source.Dispatcher();
            unsigned downs=0,ups=0,updates=0,cancellations=0;std::set<unsigned> active;bool ownerThread=true;
            const DWORD uiThread=GetCurrentThreadId();Json coordinates=Json::array(),cancellationTrace=Json::array();bool tracing=false;
            auto record=[&](auto const& args,const char* phase){const auto p=args.CurrentPoint();const unsigned id=p.PointerId();
                ownerThread=ownerThread&&GetCurrentThreadId()==uiThread;
                if(coordinates.size()<6){POINTER_INFO info{};bool got=GetPointerInfo(id,&info)!=FALSE;coordinates.push_back({{"phase",phase},{"id",id},
                    {"positionX",p.Position().X},{"positionY",p.Position().Y},{"getPointerInfo",got},{"screenX",info.ptPixelLocation.x},{"screenY",info.ptPixelLocation.y}});}
                if(tracing){POINTER_INFO info{};bool got=GetPointerInfo(id,&info)!=FALSE;cancellationTrace.push_back({{"phase",phase},{"tick",GetTickCount64()},{"id",id},{"x",p.Position().X},{"y",p.Position().Y},{"inContact",p.IsInContact()},{"pointCancelled",p.Properties().IsCanceled()},{"device",(int)p.PointerDevice().PointerDeviceType()},{"getPointerInfo",got},{"flags",info.pointerFlags},{"activeCountBefore",active.size()},{"coreDownBefore",core.pads[0].down}});}
                return id;};
            source.PointerPressed([&](auto const&,auto const& args){++downs;const auto id=record(args,"down");active.insert(id);if(adaptCore)windowProc(child,WM_POINTERDOWN,MAKEWPARAM(id,0),0);args.Handled(true);});
            source.PointerReleased([&](auto const&,auto const& args){++ups;const auto id=record(args,"up");active.erase(id);if(adaptCore)windowProc(child,WM_POINTERUP,MAKEWPARAM(id,0),0);args.Handled(true);});
            source.PointerMoved([&](auto const&,auto const& args){++updates;const auto id=record(args,"move");if(adaptCore&&active.count(id))windowProc(child,WM_POINTERUPDATE,MAKEWPARAM(id,0),0);args.Handled(true);});
            source.PointerCaptureLost([&](auto const&,auto const& args){++cancellations;const auto id=args.CurrentPoint().PointerId();active.erase(id);if(adaptCore)windowProc(child,WM_POINTERCAPTURECHANGED,MAKEWPARAM(id,0),0);args.Handled(true);});
            pump(40);
            const POINT controlPoint{430,340};const auto controlDownBefore=downs,controlUpBefore=ups;const auto lowerControlBefore=shared->touchDowns;const auto lowerPenControlBefore=shared->penDowns;
            const unsigned controlContacts=lifecycle<2?2u:1u;bool control=lifecycle<2?touchFrame(target,controlPoint,2,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT):penFrame(target,pen,controlPoint,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
            if(lifecycle<2){Sleep(3);control=touchFrame(target,controlPoint,2,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&control;}pump(40);
            const auto opaqueDeadline=GetTickCount64()+300;while(active.size()!=controlContacts&&GetTickCount64()<opaqueDeadline)pump(5);
            const bool opaqueHeld=active.size()==controlContacts;
            control=(lifecycle<2?touchFrame(target,controlPoint,2,POINTER_FLAG_UP):penFrame(target,pen,controlPoint,POINTER_FLAG_UP))&&control;pump(40);
            const auto opaqueUpDeadline=GetTickCount64()+300;while(ups-controlUpBefore<controlContacts&&GetTickCount64()<opaqueUpDeadline)pump(5);
            control=control&&downs-controlDownBefore==controlContacts&&ups-controlUpBefore==controlContacts&&shared->touchDowns==lowerControlBefore&&shared->penDowns==lowerPenControlBefore&&active.empty();
            result["opaqueControls"].push_back({{"lifecycle",lifecycle},{"valid",control},{"heldBeforeUp",opaqueHeld}});valid=valid&&control;
            SetWindowLongPtrW(root,GWL_EXSTYLE,GetWindowLongPtrW(root,GWL_EXSTYLE)|WS_EX_LAYERED);
            if(!uploadEmptyInput(root))throw std::runtime_error("zero-alpha-upload-failed");
            SetWindowPos(root,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);pump(100);
            for(unsigned geometry=0;geometry<3;++geometry) {
                const POINT origin=geometry==0?POINT{360,280}:(geometry==1?POINT{340,260}:POINT{380,300});
                const SIZE size=geometry==0?SIZE{260,200}:SIZE{300,220};
                SetWindowPos(root,HWND_TOPMOST,origin.x,origin.y,size.cx,size.cy,SWP_NOACTIVATE|SWP_SHOWWINDOW);
                SetWindowPos(child,nullptr,0,0,size.cx,size.cy,SWP_NOACTIVATE|SWP_NOZORDER);sprite.Size({(float)size.cx,(float)size.cy});sizes[0]=sizes[1]=size;
                SetWindowPos(outlines[0],HWND_TOPMOST,origin.x,origin.y,size.cx,size.cy,SWP_NOACTIVATE|SWP_SHOWWINDOW);
                if(geometry<2&&!uploadEmptyInput(root))throw std::runtime_error("resize-zero-alpha-failed");pump(40);
                for(unsigned round=0;round<(lifecycle<2?9u:3u);++round) {
                    const bool isPen=lifecycle>=2;const unsigned contacts=isPen?1:(round%2+1);const POINT point{origin.x+70,origin.y+60};
                    const LONG mouseBefore=shared->leftDowns,upBefore=shared->leftUps,wheelBefore=shared->wheels,movesBefore=shared->moves,dragBefore=shared->dragMoves;
                    const LONG touchBefore=shared->touchDowns,touchUpBefore=shared->touchUps,penBefore=shared->penDowns;
                    const auto sourceBefore=downs,releasedBefore=ups,movedBefore=updates;const auto eventsBefore=eventCount;
                    const DWORD downFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT,moveFlags=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;
                    bool inputOk=true;SetLastError(0);
                    // A mouse-down and a touch/pen-down deliberately overlap without UI pumping.
                    if(round%3==0)inputOk=mousePart(target,point,MOUSEEVENTF_LEFTDOWN)&&inputOk;
                    inputOk=(isPen?penFrame(target,pen,point,downFlags):touchFrame(target,point,contacts,downFlags))&&inputOk;
                    if(round%3!=0)inputOk=mousePart(target,point,MOUSEEVENTF_LEFTDOWN)&&inputOk;
                    Sleep(3);POINT moved{point.x+6,point.y+5};
                    inputOk=(isPen?penFrame(target,pen,moved,moveFlags):touchFrame(target,moved,contacts,moveFlags))&&inputOk;
                    inputOk=mousePart(target,moved)&&inputOk;Sleep(3);
                    POINT end{point.x+12,point.y+10};inputOk=(isPen?penFrame(target,pen,end,moveFlags):touchFrame(target,end,contacts,moveFlags))&&inputOk;
                    inputOk=mousePart(target,end,MOUSEEVENTF_LEFTUP,true)&&inputOk;finalCursor=end;pump(60);
                    const bool coreHeld=!adaptCore||(core.pads[0].down&&(cfg.single!=Mode::DualSense||contacts==1||core.pads[1].down));
                    auto samePoint=[](const Pad& p,float x,float y){return p.down&&std::abs(p.x-x)<=1&&std::abs(p.y-y)<=1;};
                    const float ex=(float)(end.x-origin.x),ey=(float)(end.y-origin.y);bool coreCoordinates=true;
                    if(adaptCore){if(cfg.single==Mode::DualSense&&contacts==2)coreCoordinates=(samePoint(core.pads[0],ex,ey)&&samePoint(core.pads[1],ex+30,ey+20))||(samePoint(core.pads[1],ex,ey)&&samePoint(core.pads[0],ex+30,ey+20));
                        else coreCoordinates=samePoint(core.pads[0],ex,ey)||(contacts==2&&samePoint(core.pads[0],ex+30,ey+20));}
                    inputOk=(isPen?penFrame(target,pen,end,POINTER_FLAG_UP):touchFrame(target,end,contacts,POINTER_FLAG_UP))&&inputOk;const DWORD inputError=inputOk?0:GetLastError();pump(60);
                    const auto pressed=downs-sourceBefore,released=ups-releasedBefore,movedEvents=updates-movedBefore;
                    const bool sample=inputOk&&shared->leftDowns-mouseBefore==1&&shared->leftUps-upBefore==1&&shared->wheels-wheelBefore==1&&
                        shared->moves-movesBefore>=2&&shared->dragMoves-dragBefore>=1&&shared->touchDowns-touchBefore==0&&shared->touchUps-touchUpBefore==0&&shared->penDowns-penBefore==0&&
                        pressed==contacts&&released==contacts&&movedEvents>=contacts&&active.empty()&&ownerThread&&coreHeld&&coreCoordinates&&
                        (adaptCore?(eventCount>eventsBefore&&!core.pads[0].down&&!core.pads[1].down&&sentKeys==0&&!sentClick):eventCount==eventsBefore);
                    valid=valid&&inputOk;passed=passed&&sample;
                    result["cases"].push_back({{"lifecycle",lifecycle},{"geometry",geometry},{"round",round},{"device",isPen?"pen":"touch"},{"contacts",contacts},
                        {"inputOk",inputOk},{"inputError",inputError},{"targetMouseDowns",shared->leftDowns-mouseBefore},{"targetMouseUps",shared->leftUps-upBefore},
                        {"targetWheelEvents",shared->wheels-wheelBefore},{"targetMouseMoves",shared->moves-movesBefore},{"targetDragMoves",shared->dragMoves-dragBefore},
                        {"targetTouchDowns",shared->touchDowns-touchBefore},{"targetTouchUps",shared->touchUps-touchUpBefore},{"targetPenDowns",shared->penDowns-penBefore},
                        {"sourceDowns",pressed},{"sourceUps",released},{"sourceMoves",movedEvents},{"activeContacts",active.size()},{"callbackOnUiThread",ownerThread},
                        {"legacyPadPointerEvents",eventCount-eventsBefore},{"mode",modeName(cfg.single)},{"coreHeldDuringContact",coreHeld},{"coreCoordinatesMatch",coreCoordinates},{"coreReleased",!core.pads[0].down&&!core.pads[1].down&&!sentKeys&&!sentClick},{"jointAcceptance",sample}});
                }
            }
            if(lifecycle<2) {
                tracing=true;const unsigned stationaryContacts=cfg.single==Mode::DualSense?2u:1u;const POINT point{450,360};const LONG lowerTouch=shared->touchDowns,lowerUps=shared->touchUps,lowerMoves=shared->touchUpdates;
                const auto cancelBefore=downs;const auto waitStart=GetTickCount64();bool injected=touchFrame(target,point,stationaryContacts,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
                Sleep(3);injected=touchFrame(target,point,stationaryContacts,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;
                while(downs-cancelBefore<stationaryContacts&&GetTickCount64()-waitStart<300)pump(5);
                const auto heldActiveCount=active.size();const bool heldCoreDown=core.pads[0].down;const auto heldCoreId=core.pads[0].id;const auto firstWaitMs=GetTickCount64()-waitStart;const bool held=active.size()==stationaryContacts&&(!adaptCore||(core.pads[0].down&&(stationaryContacts==1||core.pads[1].down)));
                // Stationary holding is reported with identical-position digitizer frames; no pointer movement is invented.
                bool stationaryHold=held;const auto holdUntil=GetTickCount64()+500;
                while(GetTickCount64()<holdUntil){Sleep(20);injected=touchFrame(target,point,stationaryContacts,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(10);stationaryHold=stationaryHold&&active.size()==stationaryContacts&&(!adaptCore||(core.pads[0].down&&(stationaryContacts==1||core.pads[1].down)));}
                result["stationaryHolds"].push_back({{"lifecycle",lifecycle},{"contacts",stationaryContacts},{"sameCoordinatesForAllFrames",true},{"durationMs",500},{"inputOk",injected},{"heldThroughout",stationaryHold}});passed=passed&&stationaryHold;
                // The actual production cancel handler releases outputs, not physical input.
                if(adaptCore)windowProc(child,WM_CANCELMODE,0,0);
                const bool released=!core.pads[0].down&&!core.pads[1].down&&!sentKeys&&!sentClick;
                Sleep(3);injected=touchFrame(target,point,stationaryContacts,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;pump(30);
                injected=touchFrame(target,point,stationaryContacts,POINTER_FLAG_UP)&&injected;pump(40);
                const bool cancelled=injected&&held&&released&&active.empty()&&shared->touchDowns==lowerTouch&&shared->touchUps==lowerUps&&shared->touchUpdates==lowerMoves&&
                    !core.pads[0].down&&!core.pads[1].down&&!sentKeys&&!sentClick;
                result["cancellation"].push_back({{"lifecycle",lifecycle},{"inputOk",injected},{"heldBeforeCancel",held},{"holdDownEvents",downs-cancelBefore},{"downWaitMs",firstWaitMs},{"heldActiveCount",heldActiveCount},{"heldCoreDown",heldCoreDown},{"heldCoreId",heldCoreId},{"coreReleasedBeforeUp",released},{"passed",cancelled}});
                valid=valid&&injected;passed=passed&&cancelled;
                const auto disableDownBefore=downs;const auto disableStart=GetTickCount64();injected=touchFrame(target,point,stationaryContacts,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
                Sleep(3);injected=touchFrame(target,point,stationaryContacts,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT)&&injected;
                while(downs-disableDownBefore<stationaryContacts&&GetTickCount64()-disableStart<300)pump(5);const bool heldBeforeDisable=active.size()==stationaryContacts&&(!adaptCore||(core.pads[0].down&&(stationaryContacts==1||core.pads[1].down)));
                Config disabled=cfg;disabled.enabled=false;configure(disabled);pump(30);
                const bool hidden=!IsWindowVisible(root)&&!core.pads[0].down&&!core.pads[1].down&&!sentKeys&&!sentClick;
                const LONG lowerMouse=shared->leftDowns,lowerMouseUp=shared->leftUps,lowerWheel=shared->wheels;
                injected=mousePart(target,point,MOUSEEVENTF_LEFTDOWN)&&injected;
                injected=mousePart(target,point,MOUSEEVENTF_LEFTUP,true)&&injected;
                injected=touchFrame(target,point,stationaryContacts,POINTER_FLAG_UP)&&injected;pump(40);
                const bool disabledSample=injected&&heldBeforeDisable&&hidden&&active.empty()&&shared->touchDowns==lowerTouch&&shared->touchUps==lowerUps&&shared->touchUpdates==lowerMoves&&
                    shared->leftDowns-lowerMouse==1&&shared->leftUps-lowerMouseUp==1&&shared->wheels-lowerWheel==1;
                valid=valid&&injected;passed=passed&&disabledSample;
                configure(cfg);SetWindowPos(root,HWND_TOPMOST,380,300,300,220,SWP_NOACTIVATE|SWP_SHOWWINDOW);
                SetWindowPos(child,nullptr,0,0,300,220,SWP_NOACTIVATE|SWP_NOZORDER);sprite.Size({300,220});sizes[0]=sizes[1]={300,220};
                SetWindowPos(outlines[0],HWND_TOPMOST,380,300,300,220,SWP_NOACTIVATE|SWP_SHOWWINDOW);
                const bool uploaded=uploadEmptyInput(root);pump(60);const auto reopenedBefore=downs;
                injected=touchFrame(target,point,1,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(40);
                injected=touchFrame(target,point,1,POINTER_FLAG_UP)&&injected;pump(40);
                const bool reopened=uploaded&&injected&&downs-reopenedBefore==1&&active.empty()&&shared->touchDowns==lowerTouch&&shared->touchUps==lowerUps&&shared->touchUpdates==lowerMoves&&
                    !core.pads[0].down&&!core.pads[1].down&&!sentKeys&&!sentClick;
                result["disableAndReenable"].push_back({{"lifecycle",lifecycle},{"heldBeforeDisable",heldBeforeDisable},{"disabledPassed",disabledSample},{"reopenedPassed",reopened}});
                valid=valid&&injected&&uploaded;passed=passed&&reopened;
            }
            // Touch trials precede all pen trials: avoid cross-device palm-rejection ambiguity.
            if(lifecycle<2){
            // The source must not swallow touch elsewhere on the owned lower target.
            POINT outside{720,510};const auto lowerBefore=shared->touchDowns;const auto sourceBefore=downs;
            bool outsideOk=touchFrame(target,outside,1,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(40);
            outsideOk=touchFrame(target,outside,1,POINTER_FLAG_UP)&&outsideOk;pump(40);
            const bool limited=outsideOk&&shared->touchDowns-lowerBefore==1&&downs==sourceBefore;passed=passed&&limited;valid=valid&&outsideOk;
            result["bounds"].push_back({{"lifecycle",lifecycle},{"outsideInputOk",outsideOk},{"outsideTouchPassed",limited}});
            }
            result["coordinateSamples"].push_back(coordinates);result["cancellationTrace"].push_back(cancellationTrace);result["captureLostCount"].push_back(cancellations);
            inputDispatcher=nullptr;controller.Close();source=nullptr;desktop.Close();DestroyWindow(child);shutdown();pump(30);
        }
        compositor.Close();
    }catch(const std::exception& error){result["failure"]=error.what();valid=false;passed=false;shutdown();}
     catch(const winrt::hresult_error& error){result["failure"]=(long)error.code();valid=false;passed=false;shutdown();}
    if(pen)DestroySyntheticPointerDevice(pen);
    result["ownedTargetTerminal"]=target.stop();valid=valid&&result["ownedTargetTerminal"].get<bool>();result["diagnosticValid"]=valid;result["candidateStressPassed"]=valid&&passed;
    POINT now{};GetCursorPos(&now);if(now.x==finalCursor.x&&now.y==finalCursor.y)SetCursorPos(oldCursor.x,oldCursor.y);
    if(oldForeground&&IsWindow(oldForeground))SetForegroundWindow(oldForeground);
    std::ofstream output(std::filesystem::path(argv[1]),std::ios::binary);output<<result.dump(2);
    std::cout<<"NATIVE_TOPOLOGY_STRESS valid="<<valid<<" candidatePassed="<<(valid&&passed)<<" samples="<<result["cases"].size()<<"\n";
    return valid&&passed&&output.good()?0:2;
}