// Real cross-process Windows input gate. No deployed product/config/driver writes.
// A diagnostic exit 0 is NOT evidence that mouse passthrough is implemented.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
#include <thread>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.Devices.Input.h>
#include <windows.ui.composition.interop.h>
#include <DispatcherQueue.h>
#pragma comment(lib,"CoreMessaging.lib")
#pragma comment(lib,"WindowsApp.lib")
using namespace ymcc::screenpads;
struct Shared {DWORD magic,owner;volatile LONG leftDowns,touchDowns,wheels;ULONG_PTR window;};
static Shared* shared=nullptr;
static WNDPROC original=nullptr;
static int variant=0;
static unsigned overlayMouse=0;
static Json sources=Json::array();
static void pump(DWORD ms) {
    const auto end=GetTickCount64()+ms;MSG m{};
    do {while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))DispatchMessageW(&m);Sleep(1);}while(GetTickCount64()<end);
}
static LRESULT CALLBACK targetProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_LBUTTONDOWN){const auto extra=(ULONG_PTR)GetMessageExtraInfo();
        if((extra&0xffffff00u)!=0xff515700u)InterlockedIncrement(&shared->leftDowns);return 0;}
    if(m==WM_MOUSEWHEEL){InterlockedIncrement(&shared->wheels);return 0;}
    if(m==WM_POINTERDOWN){POINTER_INPUT_TYPE type{};if(GetPointerType(GET_POINTERID_WPARAM(w),&type)&&type==PT_TOUCH)InterlockedIncrement(&shared->touchDowns);return 0;}
    if(m==WM_POINTERUP||m==WM_POINTERUPDATE)return 0;
    if(m==WM_CLOSE||m==WM_TIMER){DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK observed(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_NCHITTEST){INPUT_MESSAGE_SOURCE source{};const bool known=GetCurrentInputMessageSource(&source)!=FALSE;
        if(sources.size()<100)sources.push_back({{"variant",variant},{"known",known},{"device",source.deviceType},{"extra",(unsigned long long)GetMessageExtraInfo()}});
        if(variant==1||variant==7)return HTTRANSPARENT;}
    if(m==WM_LBUTTONDOWN)++overlayMouse;
    return CallWindowProcW(original,h,m,w,l);
}
static int targetMain(const wchar_t* mapping) {
    if(std::wstring(mapping).rfind(L"Local\\YmccOwnMousePassGate_",0)!=0)return 4;
    HANDLE map=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,mapping);if(!map)return 5;
    shared=(Shared*)MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));
    if(!shared||shared->magic!=0x594d5047u||!shared->owner)return 6;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YmccOwnMousePassGateTarget";
    cls.lpfnWndProc=targetProc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    if(!RegisterClassW(&cls))return 7;
    const HWND h=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,cls.lpszClassName,L"YMCC 自有输入验证（非游戏）",WS_POPUP|WS_VISIBLE,300,220,500,340,nullptr,nullptr,cls.hInstance,nullptr);
    if(!h)return 8;shared->window=(ULONG_PTR)h;SetTimer(h,1,30000,nullptr);
    MSG m{};while(GetMessageW(&m,nullptr,0,0)>0)DispatchMessageW(&m);
    UnmapViewOfFile(shared);CloseHandle(map);return 0;
}
struct OwnTarget {
    HANDLE map=nullptr;PROCESS_INFORMATION child{};HWND window=nullptr;bool terminal=false;
    bool start() {
        const auto name=L"Local\\YmccOwnMousePassGate_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
        map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name.c_str());
        if(!map||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        shared=(Shared*)MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));if(!shared)return false;
        *shared={0x594d5047u,GetCurrentProcessId(),0,0,0,0};wchar_t exe[32768]{};
        if(!GetModuleFileNameW(nullptr,exe,(DWORD)std::size(exe)))return false;
        auto command=L"\""+std::wstring(exe)+L"\" --target \""+name+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);
        if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return false;
        CloseHandle(child.hThread);child.hThread=nullptr;
        for(int i=0;i<2500;i++){window=(HWND)shared->window;if(window)break;if(WaitForSingleObject(child.hProcess,0)!=WAIT_TIMEOUT)break;Sleep(2);}
        DWORD pid=0;return window&&GetWindowThreadProcessId(window,&pid)&&pid==child.dwProcessId;
    }
    bool owns(HWND h) const {DWORD pid=0;return h&&GetWindowThreadProcessId(h,&pid)&&pid==child.dwProcessId;}
    bool stop() {
        if(!child.hProcess)return true;
        if(owns(window))PostMessageW(window,WM_CLOSE,0,0);
        else PostThreadMessageW(child.dwThreadId,WM_QUIT,0,0);
        terminal=WaitForSingleObject(child.hProcess,35000)==WAIT_OBJECT_0;
        if(terminal){CloseHandle(child.hProcess);child.hProcess=nullptr;}return terminal;
    }
    ~OwnTarget(){stop();if(child.hProcess)CloseHandle(child.hProcess);if(shared){UnmapViewOfFile(shared);shared=nullptr;}if(map)CloseHandle(map);}
};
static bool authorizedPoint(const OwnTarget& target,POINT point) {
    const auto h=WindowFromPoint(point);DWORD pid=0;GetWindowThreadProcessId(h,&pid);
    return pid==target.child.dwProcessId || (pid==GetCurrentProcessId() &&
        (h==windows[0]||h==outlines[0]||h==windows[1]||h==outlines[1]));
}
static bool mouseClickAndWheel(const OwnTarget& target,POINT point) {
    if(!authorizedPoint(target,point))return false;
    INPUT in[4]{};for(auto& e:in){e.type=INPUT_MOUSE;e.mi.dwExtraInfo=0x594d5047u;}
    const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN),width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
    in[0].mi.dx=MulDiv(point.x-x,65535,width-1);in[0].mi.dy=MulDiv(point.y-y,65535,height-1);
    in[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    in[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;in[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
    in[3].mi.dwFlags=MOUSEEVENTF_WHEEL;in[3].mi.mouseData=WHEEL_DELTA;
    return SendInput((UINT)std::size(in),in,sizeof(INPUT))==std::size(in);
}
static bool injectContact(const OwnTarget& target,POINT point,bool down) {
    if(!authorizedPoint(target,point))return false;
    POINTER_TOUCH_INFO contact{};contact.pointerInfo.pointerType=PT_TOUCH;contact.pointerInfo.pointerId=0;contact.pointerInfo.ptPixelLocation=point;
    contact.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_PRESSURE;contact.rcContact={point.x-3,point.y-3,point.x+3,point.y+3};contact.pressure=512;
    contact.pointerInfo.pointerFlags=down?(POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT):POINTER_FLAG_UP;
    return InjectTouchInput(1,&contact)!=FALSE;
}
static bool uploadEmptyInput(HWND h) {
    RECT r{};if(!GetWindowRect(h,&r))return false;SIZE size{r.right-r.left,r.bottom-r.top};POINT dst{r.left,r.top},src{};
    HDC dc=GetDC(nullptr),memory=CreateCompatibleDC(dc);BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=size.cx;info.bmiHeader.biHeight=-size.cy;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;HBITMAP bmp=CreateDIBSection(dc,&info,DIB_RGB_COLORS,&bits,nullptr,0);
    bool ok=false;if(bmp&&bits){auto old=SelectObject(memory,bmp);std::fill_n((DWORD*)bits,size.cx*size.cy,0u);
        BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};ok=UpdateLayeredWindow(h,dc,&dst,&size,memory,&src,0,&blend,ULW_ALPHA)!=FALSE;SelectObject(memory,old);}
    if(bmp)DeleteObject(bmp);if(memory)DeleteDC(memory);if(dc)ReleaseDC(nullptr,dc);return ok;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target")return targetMain(argv[2]);
    if(argc<2||argc>3)return 2;const bool requirePass=argc==3&&std::wstring(argv[2])==L"--require-pass";
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT oldCursor{};GetCursorPos(&oldCursor);const HWND oldForeground=GetForegroundWindow();
    Json result={{"goal","mouse passes to a different process while touch stays with screen pad"},{"featureAcceptancePassed",false},
        {"diagnosticValid",false},{"productionCodeChanged",false},{"productDeployed",false},{"configurationWrites",0},{"virtualTargetStarted",false},
        {"testMouseInjected",true},{"testTouchInjected",true},{"touchpadSystemOutputInjected",false},{"clickForwarded",false},{"globalHookInstalled",false},{"uiAccessChanged",false},{"cases",Json::array()}};
    OwnTarget target;bool valid=true,passingCandidate=false;
    try {
        if(!target.start())throw std::runtime_error("own-target-not-ready");
        result["childPid"]=target.child.dwProcessId;
        if(!InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch-injection-unavailable");
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        winrt::Windows::System::DispatcherQueueController queue{nullptr};
        DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
        winrt::check_hresult(CreateDispatcherQueueController(options,(ABI::Windows::System::IDispatcherQueueController**)winrt::put_abi(queue)));
        winrt::Windows::UI::Composition::Compositor compositor;
        const std::array<const char*,8> names={"production","HTTRANSPARENT-only","WS_EX_TRANSPARENT-without-layered","monitor-layered-transparent",
            "per-pixel-zero","controlled-touch-with-uncontrolled-input-transparent","controller-plus-monitor-style","controller-plus-HTTRANSPARENT"};
        for(variant=0;variant<8;variant++) {
            // Fresh native windows for EACH sample: no stale cached hit test/style.
            Config cfg=profileDefault(0);cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;
            dryRun=true;setContext("disabled",false);initialize(GetModuleHandleW(nullptr),nullptr,cfg,0);refresh();
            const HWND input=windows[0];if(!input)throw std::runtime_error("production-input-window-missing");
            SetWindowPos(target.window,HWND_TOPMOST,300,220,500,340,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(input,HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);
            SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]={260,200};
            original=(WNDPROC)SetWindowLongPtrW(input,GWLP_WNDPROC,(LONG_PTR)observed);
            const auto base=GetWindowLongPtrW(input,GWL_EXSTYLE);bool setupOk=true;
            winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget desktop{nullptr};
            winrt::Windows::UI::Core::CoreIndependentInputSourceController controller{nullptr};
            winrt::Windows::UI::Core::CoreIndependentInputSource source{nullptr};
            unsigned sourceDowns=0,sourceUps=0;Json pointerDetails=Json::array();
            if(variant>=5) {
                try {
                    auto interop=compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
                    winrt::check_hresult(interop->CreateDesktopWindowTarget(input,TRUE,(ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**)winrt::put_abi(desktop)));
                    auto sprite=compositor.CreateSpriteVisual();sprite.Size({260,200});sprite.Brush(compositor.CreateColorBrush({0,0,0,0}));desktop.Root(sprite);
                    controller=winrt::Windows::UI::Core::CoreIndependentInputSourceController::CreateForVisual(sprite);
                    controller.SetControlledInput(winrt::Windows::UI::Core::CoreInputDeviceTypes::Touch|winrt::Windows::UI::Core::CoreInputDeviceTypes::Pen);
                    controller.IsTransparentForUncontrolledInput(true);source=controller.Source();
                    source.PointerPressed([&](auto const&,auto const& args){++sourceDowns;auto p=args.CurrentPoint();pointerDetails.push_back({{"id",p.PointerId()},{"x",p.Position().X},{"y",p.Position().Y},{"phase","down"}});args.Handled(true);});
                    source.PointerReleased([&](auto const&,auto const& args){++sourceUps;args.Handled(true);});
                }catch(const winrt::hresult_error& error){setupOk=false;pointerDetails.push_back({{"apiError",(long)error.code()}});}
            }
            if(variant==2)SetWindowLongPtrW(input,GWL_EXSTYLE,base|WS_EX_TRANSPARENT);
            if(variant==3||variant==6){SetWindowLongPtrW(input,GWL_EXSTYLE,base|WS_EX_LAYERED|WS_EX_TRANSPARENT);setupOk=SetLayeredWindowAttributes(input,0,255,LWA_ALPHA)!=FALSE&&setupOk;}
            if(variant==4){SetWindowLongPtrW(input,GWL_EXSTYLE,base|WS_EX_LAYERED);setupOk=uploadEmptyInput(input)&&setupOk;}
            SetWindowPos(input,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);pump(100);
            POINT point{450,350};
            for(int burst=0;burst<2;burst++) {
                const LONG mouseBefore=shared->leftDowns,touchBefore=shared->touchDowns,wheelBefore=shared->wheels;
                const auto eventsBefore=eventCount;const auto padMouseBefore=overlayMouse,downBefore=sourceDowns,upBefore=sourceUps;
                const bool mouseSent=mouseClickAndWheel(target,point);
                if(!burst)pump(80); // Second sample overlaps new mouse + touch.
                SetLastError(0);const bool down=injectContact(target,point,true);const DWORD downError=GetLastError();pump(60);
                const bool up=injectContact(target,point,false);const DWORD upError=GetLastError();pump(90);
                const LONG mouse=shared->leftDowns-mouseBefore,touches=shared->touchDowns-touchBefore,wheel=shared->wheels-wheelBefore;
                const auto events=eventCount-eventsBefore;const auto pressed=sourceDowns-downBefore,released=sourceUps-upBefore;
                const bool touchCaptured=variant>=5?(pressed==1&&released==1):(events>=2);
                const bool both=setupOk&&mouseSent&&down&&up&&mouse==1&&wheel==1&&touchCaptured&&touches==0;
                bool positiveControl=true;
                if(variant==0)positiveControl=mouse==0&&touchCaptured&&touches==0;
                if(variant==3||variant==4)positiveControl=mouse==1&&!touchCaptured&&touches==1;
                valid=valid&&positiveControl&&setupOk&&mouseSent&&down&&up;
                if(variant>=1&&both)passingCandidate=true;
                result["cases"].push_back({{"name",names[variant]},{"variant",variant},{"freshInputWindow",true},{"overlappedBurst",burst!=0},
                    {"setupOk",setupOk},{"mouseInjected",mouseSent},{"touchDownInjected",down},{"touchUpInjected",up},{"downError",downError},{"upError",upError},
                    {"targetMouseClicks",mouse},{"targetWheelEvents",wheel},{"targetTouchDowns",touches},{"padPointerEvents",events},{"padMouseClicks",overlayMouse-padMouseBefore},
                    {"independentSourceDowns",pressed},{"independentSourceUps",released},{"touchCaptured",touchCaptured},{"mouseAndTouchAcceptance",both},{"positiveControlValid",positiveControl}});
            }
            result["cases"].back()["pointerDetails"]=pointerDetails;
            if(controller)controller.Close();source=nullptr;if(desktop)desktop.Close();
            SetWindowLongPtrW(input,GWLP_WNDPROC,(LONG_PTR)original);shutdown();pump(30);
        }
        compositor.Close();
        result["hitTestSources"]=sources;result["diagnosticValid"]=valid;
        // A single successful variant/sample would still need a full regression
        // matrix and production event adapter before product completion.
        result["candidateFound"]=passingCandidate;
        result["featureAcceptancePassed"]=false;
    }catch(const std::exception& error){result["failure"]=error.what();valid=false;shutdown();}
     catch(const winrt::hresult_error& error){result["failure"]=(long)error.code();valid=false;shutdown();}
    result["ownedTargetTerminal"]=target.stop();valid=valid&&result["ownedTargetTerminal"].get<bool>();result["diagnosticValid"]=valid;
    POINT now{};GetCursorPos(&now);if(now.x==450&&now.y==350)SetCursorPos(oldCursor.x,oldCursor.y);
    if(oldForeground&&IsWindow(oldForeground))SetForegroundWindow(oldForeground);
    std::ofstream out(std::filesystem::path(argv[1]),std::ios::binary);out<<result.dump(2);
    std::cout<<result.dump(2)<<"\n";
    if(!valid||!out.good())return 2;
    return requirePass&&!result["featureAcceptancePassed"].get<bool>()?3:0;
}
