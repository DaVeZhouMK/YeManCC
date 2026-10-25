#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "gesture_summon.h"
#include "json.hpp"
#include <iostream>
#include <stdexcept>
using nlohmann::json;
using ymcc::gesturesummon::EdgeSurface;
static EdgeSurface* edge=nullptr;
static unsigned summons=0,accepted=0,frames=0,stageTouches=0,stageMouse=0,stageWheels=0;
static constexpr ULONG_PTR mouseTag=0x594d4753u;
static HWND owner=nullptr,stage=nullptr;
static RECT stageArea{};
static bool owns(HWND h){DWORD pid=0;return h&&GetWindowThreadProcessId(h,&pid)&&pid==GetCurrentProcessId();}
static void pump(DWORD ms){auto end=GetTickCount64()+ms;MSG msg{};do{while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}Sleep(1);}while(GetTickCount64()<end);}
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==ymcc::gesturesummon::kSummonMessage){++summons;if(edge&&edge->accepts((UINT_PTR)w)){++accepted;ShowWindow(h,SW_SHOWNOACTIVATE);}return 0;}
    if(h==stage&&m==WM_POINTERDOWN){POINTER_INPUT_TYPE type{};if(GetPointerType(GET_POINTERID_WPARAM(w),&type)&&type==PT_TOUCH)++stageTouches;}
    if(h==stage&&m==WM_LBUTTONDOWN&&(ULONG_PTR)GetMessageExtraInfo()==mouseTag)++stageMouse;
    if(h==stage&&m==WM_MOUSEWHEEL&&(ULONG_PTR)GetMessageExtraInfo()==mouseTag)++stageWheels;
    if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
    if(m==WM_POINTERACTIVATE)return PA_NOACTIVATE;
    if(m==WM_PAINT){PAINTSTRUCT p{};HDC dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(230,235,240));DrawTextW(dc,L"YMCC touch gesture test",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);EndPaint(h,&p);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static bool mousePass(POINT p) {
    if (!owns(WindowFromPoint(p))) return false;
    const unsigned clicks=stageMouse,wheels=stageWheels;
    INPUT in[4]{};for(auto& event:in){event.type=INPUT_MOUSE;event.mi.dwExtraInfo=mouseTag;}
    const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN);
    in[0].mi.dx=MulDiv(p.x-x,65535,GetSystemMetrics(SM_CXVIRTUALSCREEN)-1);in[0].mi.dy=MulDiv(p.y-y,65535,GetSystemMetrics(SM_CYVIRTUALSCREEN)-1);
    in[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
    in[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;in[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;in[3].mi.dwFlags=MOUSEEVENTF_WHEEL;in[3].mi.mouseData=120;
    if(SendInput(4,in,sizeof(INPUT))!=4)return false;pump(30);
    return stageMouse==clicks+1&&stageWheels==wheels+1;
}
static void inject(POINT p,DWORD flags,int count=1){
    if(!owns(stage)||!owns(WindowFromPoint(p))||!PtInRect(&stageArea,p))throw std::runtime_error("input destination is outside owned test windows");
    POINTER_TOUCH_INFO touch[2]{};
    for(int i=0;i<count;++i){auto& t=touch[i];t.pointerInfo.pointerType=PT_TOUCH;t.pointerInfo.pointerId=i;t.pointerInfo.pointerFlags=flags;t.pointerInfo.ptPixelLocation={p.x,p.y+i*30};
        if(!PtInRect(&stageArea,t.pointerInfo.ptPixelLocation))throw std::runtime_error("second contact outside test window");
        t.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;t.orientation=90;t.pressure=512;
        const auto pt=t.pointerInfo.ptPixelLocation;t.rcContact={pt.x-2,pt.y-2,pt.x+2,pt.y+2};}
    if(!InjectTouchInput(count,touch))throw std::runtime_error("InjectTouchInput failed: "+std::to_string(GetLastError()));++frames;
}
int main(){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    POINT savedCursor{};GetCursorPos(&savedCursor);const HWND priorForeground=GetForegroundWindow();
    json report={{"touchInjected",true},{"physicalTouch",false},{"installedSettingsChanged",false},{"cases",json::array()}};
    bool ok=true;
    try{
        winrt::init_apartment(winrt::apartment_type::single_threaded);
        WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YmccOwnGestureTouchTest";cls.lpfnWndProc=proc;cls.hbrBackground=CreateSolidBrush(RGB(20,29,43));RegisterClassW(&cls);
        MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromPoint(savedCursor,MONITOR_DEFAULTTOPRIMARY),&monitor);
        const RECT area=monitor.rcWork;const int y=area.top+(area.bottom-area.top-260)/2;
        owner=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"YMCC gesture owner",WS_POPUP,area.left+50,y+40,220,150,nullptr,nullptr,cls.hInstance,nullptr);
        stage=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_TOPMOST|WS_EX_NOACTIVATE,cls.lpszClassName,L"YMCC touch gesture test",WS_POPUP,area.left,y,380,260,nullptr,nullptr,cls.hInstance,nullptr);
        if(!owner||!stage)throw std::runtime_error("test HWND unavailable");
        if(!InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE))throw std::runtime_error("touch injection unavailable");
        EdgeSurface surface;edge=&surface;
        struct Case {const char* name;bool left;int inset,dx,dy,contacts;bool enabled,cancel,changeSide;DWORD hold;unsigned expected;bool opposite=false;int heightPercent=50;};
        const Case cases[]={
            {"left-outermost-inward",true,1,100,0,1,true,false,false,0,1},
            {"right-outermost-inward",false,1,100,0,1,true,false,false,0,1},
            {"left-edge-inner-inward",true,8,100,0,1,true,false,false,0,1},
            {"right-edge-inner-inward",false,8,100,0,1,true,false,false,0,1},
            {"tap-does-not-summon",true,3,0,0,1,true,false,false,0,0},
            {"short-swipe-does-not-summon",false,3,40,0,1,true,false,false,0,0},
            {"vertical-swipe-does-not-summon",true,3,0,90,1,true,false,false,0,0},
            {"outside-edge-does-not-summon",true,30,100,0,1,true,false,false,0,0},
            {"two-finger-swipe-does-not-summon",false,3,100,0,2,true,false,false,0,0},
            {"cancelled-release-does-not-summon",true,3,100,0,1,true,true,false,0,0},
            {"disabled-does-not-summon",false,3,0,0,1,false,false,false,0,0},
            {"side-change-cancels-contact",true,3,100,0,1,true,false,true,0,0},
            {"slow-hold-does-not-summon",false,3,100,0,1,true,false,false,1100,0},
            {"right-selected-opposite-left-pass",false,3,0,0,1,true,false,false,0,0,true},
            {"left-selected-opposite-right-pass",true,3,0,0,1,true,false,false,0,0,true},
            {"left-top-margin-pass",true,3,0,0,1,true,false,false,0,0,false,5},
            {"right-bottom-margin-pass",false,3,0,0,1,true,false,false,0,0,false,95},
            
        };
        unsigned caseIndex=0;for(const auto& c:cases){if(GetEnvironmentVariableW(L"YMCC_GESTURE_TOUCH_QUICK",nullptr,0)&&caseIndex++==4)break;
            ShowWindow(owner,SW_HIDE);surface.refresh(owner,false,c.left);pump(20);
            const bool startLeft=c.opposite?!c.left:c.left;
            const int sx=startLeft?monitor.rcMonitor.left:monitor.rcMonitor.right-380;
            const int contactY=monitor.rcMonitor.top+(monitor.rcMonitor.bottom-monitor.rcMonitor.top)*c.heightPercent/100;
            const int stageY=std::clamp(contactY-60,(int)monitor.rcMonitor.top,(int)monitor.rcMonitor.bottom-260);
            SetWindowPos(stage,HWND_TOPMOST,sx,stageY,380,260,SWP_SHOWWINDOW);GetWindowRect(stage,&stageArea);pump(40);
            const UINT dpi=GetDpiForWindow(owner);const float scale=dpi/96.0f;
            if(!surface.refresh(owner,c.enabled,c.left,true))throw std::runtime_error("edge unavailable: "+std::to_string(surface.error()));
            pump(100);
            POINT p{startLeft?monitor.rcMonitor.left+(int)(c.inset*scale):monitor.rcMonitor.right-1-(int)(c.inset*scale),contactY};
            const auto before=accepted,messages=summons,touchesBefore=stageTouches,clicksBefore=stageMouse,wheelsBefore=stageWheels;
            const bool foregroundOwned=owns(GetForegroundWindow());
            const bool mouseBefore=mousePass(p);
            inject(p,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT,c.contacts);pump(20);
            if(c.changeSide){surface.refresh(owner,true,!c.left,true);pump(30);}
            for(DWORD held=0;held<c.hold;held+=100){pump(100);inject(p,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT,c.contacts);}
            bool early=accepted!=before;
            const bool mouseDuring=c.expected?mousePass(p):true;
            const POINT start=p;
            for(int i=1;i<=6;++i){p.x=start.x+(startLeft?1:-1)*(int)(c.dx*scale*i/6);p.y=start.y+(int)(c.dy*scale*i/6);inject(p,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT,c.contacts);pump(16);early=early||accepted!=before;}
            inject(p,POINTER_FLAG_UP|(c.cancel?POINTER_FLAG_CANCELED:0),c.contacts);pump(120);
            const bool outsidePass=(!(c.opposite||c.heightPercent!=50||std::string(c.name)=="outside-edge-does-not-summon"))||stageTouches==touchesBefore+1;
            const bool pass=!early&&accepted-before==c.expected&&IsWindowVisible(owner)==(c.expected!=0)&&surface.available()&&mouseBefore&&mouseDuring&&outsidePass;
            report["cases"].push_back({{"name",c.name},{"pass",pass},{"accepted",accepted-before},{"messages",summons-messages},{"earlySummon",early},{"available",surface.available()},{"error",(long)surface.error()},{"ownerShown",IsWindowVisible(owner)!=FALSE},{"dpi",dpi},{"mousePassBefore",mouseBefore},{"mousePassDuringTouch",mouseDuring},{"touchesToStage",stageTouches-touchesBefore},{"outsideEdgeTouchPass",outsidePass},{"mouseClicks",stageMouse-clicksBefore},{"mouseWheels",stageWheels-wheelsBefore},{"foregroundOwned",foregroundOwned}});ok=ok&&pass;
        }
        surface.shutdown();edge=nullptr;
    }catch(const std::exception& e){ok=false;report["error"]=e.what();edge=nullptr;}
    catch(const winrt::hresult_error& e){ok=false;report["error"]="WinRT "+std::to_string((long)e.code());edge=nullptr;}
    const bool stillOwned=owns(GetForegroundWindow());
    POINT finalCursor{};GetCursorPos(&finalCursor);
    const bool cursorStillInTest=owns(WindowFromPoint(finalCursor));
    if(stage)DestroyWindow(stage);if(owner)DestroyWindow(owner);
    if(cursorStillInTest)SetCursorPos(savedCursor.x,savedCursor.y);
    if(stillOwned&&priorForeground&&IsWindow(priorForeground))SetForegroundWindow(priorForeground);
    report["ok"]=ok;report["touchInjected"]=frames>0;report["framesInjected"]=frames;report["summons"]=accepted;
    std::cout<<report.dump(2);return ok?0:1;
}
