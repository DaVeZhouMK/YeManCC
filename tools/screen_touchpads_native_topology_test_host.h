#pragma once
// Native region/owned-child topology probe. No deployed product/config/driver writes.
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
struct Shared {DWORD magic,owner;volatile LONG leftDowns,touchDowns,wheels,leftUps,moves,dragMoves,touchUps,touchUpdates,penDowns,penUps,manualInput,hardwareMouseEvents,manualTouchHeld,manualJointClicks,manualJointWheels,manualJointDrags;RECT manualPadRect{};volatile LONG bindingDowns[8]{},bindingUps[8]{},bindingHeldMask=0,bindingMouseMoves=0,bindingMouseDowns=0,bindingMouseUps=0,bindingOriginInjected=0,bindingMouseX=0,bindingMouseY=0;ULONG_PTR window;bool passiveTarget=false;DWORD lifetimeMs=30000;};
inline constexpr UINT kOwnedTargetPromoteMessage=WM_APP+205; // Isolated helper only.
static Shared* shared=nullptr;
static winrt::Windows::UI::Core::CoreDispatcher inputDispatcher{nullptr};
static void pump(DWORD ms) {
    const auto end=GetTickCount64()+ms;MSG m{};
    do {if(inputDispatcher)inputDispatcher.ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))DispatchMessageW(&m);Sleep(1);}while(GetTickCount64()<end);
}
static bool ownPhysicalMouse() {
    if(!shared||!shared->manualInput)return false;INPUT_MESSAGE_SOURCE input{};
    return GetCurrentInputMessageSource(&input)&&input.deviceType==IMDT_MOUSE&&input.originId==IMO_HARDWARE;
}
static LRESULT CALLBACK targetProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    // Produce a genuine CHILD-process window-order event, as a game does.
    // Never inject a click or reparent/touch any foreign application window.
    if(m==kOwnedTargetPromoteMessage&&w==0x594d5047u&&shared&&shared->owner){
        SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);return 0;
    }
    const bool bindingTag=(ULONG_PTR)GetMessageExtraInfo()==kInjectedTag;
    if(bindingTag&&(m==WM_KEYDOWN||m==WM_KEYUP)) {
        static constexpr UINT keys[]={'W','S','A','D',VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT};
        for(int i=0;i<8;++i)if(w==keys[i]){InterlockedIncrement(m==WM_KEYDOWN?&shared->bindingDowns[i]:&shared->bindingUps[i]);
            if(m==WM_KEYDOWN)InterlockedOr(&shared->bindingHeldMask,1L<<i);else InterlockedAnd(&shared->bindingHeldMask,~(1L<<i));}
        INPUT_MESSAGE_SOURCE input{};if(GetCurrentInputMessageSource(&input)&&input.originId==IMO_INJECTED)InterlockedIncrement(&shared->bindingOriginInjected);return 0;
    }
    if(bindingTag&&(m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_MOUSEMOVE)) {
        if(m==WM_LBUTTONDOWN)InterlockedIncrement(&shared->bindingMouseDowns);if(m==WM_LBUTTONUP)InterlockedIncrement(&shared->bindingMouseUps);
        if(m==WM_MOUSEMOVE){InterlockedIncrement(&shared->bindingMouseMoves);POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};ClientToScreen(h,&point);
            InterlockedExchange(&shared->bindingMouseX,point.x);InterlockedExchange(&shared->bindingMouseY,point.y);}
        INPUT_MESSAGE_SOURCE input{};if(GetCurrentInputMessageSource(&input)&&input.originId==IMO_INJECTED)InterlockedIncrement(&shared->bindingOriginInjected);
    }
    if(m==WM_PAINT&&shared->manualInput){PAINTSTRUCT paint{};HDC dc=BeginPaint(h,&paint);RECT text{};GetClientRect(h,&text);text.left+=10;text.top+=5;text.right-=10;text.bottom=55;
        wchar_t title[512]{};GetWindowTextW(h,title,(int)std::size(title));SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(0,0,0));DrawTextW(dc,title,-1,&text,DT_WORDBREAK|DT_NOPREFIX);EndPaint(h,&paint);return 0;}
    if((m==WM_LBUTTONDOWN||m==WM_LBUTTONUP||m==WM_MOUSEMOVE||m==WM_MOUSEWHEEL)&&ownPhysicalMouse()) {
        InterlockedIncrement(&shared->hardwareMouseEvents);
        POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};if(m!=WM_MOUSEWHEEL)ClientToScreen(h,&point);
        if(shared->manualTouchHeld&&PtInRect(&shared->manualPadRect,point)) {
            if(m==WM_LBUTTONDOWN)InterlockedIncrement(&shared->manualJointClicks);
            if(m==WM_MOUSEWHEEL)InterlockedIncrement(&shared->manualJointWheels);
            if(m==WM_MOUSEMOVE&&(w&MK_LBUTTON))InterlockedIncrement(&shared->manualJointDrags);
        }
    }
    if(m==WM_LBUTTONDOWN){const auto extra=(ULONG_PTR)GetMessageExtraInfo();
        if((extra&0xffffff00u)!=0xff515700u)InterlockedIncrement(&shared->leftDowns);return 0;}
    if(m==WM_LBUTTONUP){if((ULONG_PTR)GetMessageExtraInfo()==0x594d5047u||ownPhysicalMouse())InterlockedIncrement(&shared->leftUps);return 0;}
    if(m==WM_MOUSEMOVE){if((ULONG_PTR)GetMessageExtraInfo()==0x594d5047u||ownPhysicalMouse()){InterlockedIncrement(&shared->moves);if(w&MK_LBUTTON)InterlockedIncrement(&shared->dragMoves);}return 0;}
    if(m==WM_MOUSEWHEEL){InterlockedIncrement(&shared->wheels);return 0;}
    if(m==WM_POINTERDOWN){POINTER_INPUT_TYPE type{};if(GetPointerType(GET_POINTERID_WPARAM(w),&type)){if(type==PT_TOUCH)InterlockedIncrement(&shared->touchDowns);else if(type==PT_PEN)InterlockedIncrement(&shared->penDowns);}return 0;}
    if(m==WM_POINTERUP||m==WM_POINTERUPDATE){POINTER_INPUT_TYPE type{};if(GetPointerType(GET_POINTERID_WPARAM(w),&type)){if(type==PT_TOUCH)InterlockedIncrement(m==WM_POINTERUP?&shared->touchUps:&shared->touchUpdates);else if(type==PT_PEN&&m==WM_POINTERUP)InterlockedIncrement(&shared->penUps);}return 0;}
    if(m==WM_CLOSE||m==WM_TIMER){DestroyWindow(h);return 0;}
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static int targetMain(const wchar_t* mapping) {
    if(std::wstring(mapping).rfind(L"Local\\YmccOwnTopologyProbe_",0)!=0)return 4;
    HANDLE map=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,mapping);if(!map)return 5;
    shared=(Shared*)MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));
    if(!shared||shared->magic!=0x594d5047u||!shared->owner)return 6;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YmccOwnTopologyProbeTarget";
    cls.lpfnWndProc=targetProc;cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);
    if(!RegisterClassW(&cls))return 7;
    const HWND h=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|(shared->passiveTarget?WS_EX_NOACTIVATE:0),cls.lpszClassName,L"YMCC 自有输入验证（非游戏）",WS_POPUP|WS_VISIBLE,300,220,500,340,nullptr,nullptr,cls.hInstance,nullptr);
    if(!h)return 8;shared->window=(ULONG_PTR)h;SetTimer(h,1,std::clamp<DWORD>(shared->lifetimeMs,30000,60000),nullptr);
    MSG m{};while(GetMessageW(&m,nullptr,0,0)>0)DispatchMessageW(&m);
    UnmapViewOfFile(shared);CloseHandle(map);return 0;
}
struct OwnTarget {
    HANDLE map=nullptr;PROCESS_INFORMATION child{};HWND window=nullptr;bool terminal=false,bootstrapUsed=false;
    // Mouse/binding acceptance always requires foreground ownership. Passive
    // start is for a separately labelled, destination-checked TOUCH-ONLY probe.
    bool start(bool requireForeground=true,DWORD foregroundWaitMs=40,bool bootstrapOwnMouse=false) {
        const auto name=L"Local\\YmccOwnTopologyProbe_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
        map=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name.c_str());
        if(!map||GetLastError()==ERROR_ALREADY_EXISTS)return false;
        shared=(Shared*)MapViewOfFile(map,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared));if(!shared)return false;
        *shared={};shared->magic=0x594d5047u;shared->owner=GetCurrentProcessId();shared->passiveTarget=!requireForeground;shared->lifetimeMs=30000+std::min<DWORD>(foregroundWaitMs,10000);wchar_t exe[32768]{};
        if(!GetModuleFileNameW(nullptr,exe,(DWORD)std::size(exe)))return false;
        auto command=L"\""+std::wstring(exe)+L"\" --target \""+name+L"\"";STARTUPINFOW startup{};startup.cb=sizeof(startup);
        if(!CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))return false;
        CloseHandle(child.hThread);child.hThread=nullptr;
        for(int i=0;i<2500;i++){window=(HWND)shared->window;if(window)break;if(WaitForSingleObject(child.hProcess,0)!=WAIT_TIMEOUT)break;Sleep(2);}
        DWORD pid=0;if(!window||!GetWindowThreadProcessId(window,&pid)||pid!=child.dwProcessId)return false;
        if(!requireForeground)return true;
        // A prepared human may need to click the owned white test window:
        // never work around Windows' foreground lock by injecting to other apps.
        if(foregroundWaitMs>40)SetWindowTextW(window,L"请点击此白色验证窗口，取得自有测试前台（非游戏）");
        SetForegroundWindow(window);const auto end=GetTickCount64()+std::min<DWORD>(foregroundWaitMs,10000);
        do {pump(20);const HWND foreground=GetForegroundWindow();DWORD foregroundPid=0;
            if(foreground&&GetWindowThreadProcessId(foreground,&foregroundPid)&&foregroundPid==child.dwProcessId)return true;
            if(!owns(window)||WaitForSingleObject(child.hProcess,0)!=WAIT_TIMEOUT)return false;
        }while(GetTickCount64()<end);
        // Explicit SYNTHETIC-MOUSE fixture option only. Obtain activation by
        // normal Windows mouse handling on the verified child, never by
        // AttachThreadInput, foreground-lock changes or clicks on other apps.
        // Actual test inputs below still require target foreground ownership.
        if(bootstrapOwnMouse&&owns(window)){
            RECT area{};if(!GetWindowRect(window,&area))return false;
            POINT point{area.left+20,area.top+20};
            if(!owns(WindowFromPoint(point)))return false;
            const int x=GetSystemMetrics(SM_XVIRTUALSCREEN),y=GetSystemMetrics(SM_YVIRTUALSCREEN);
            const int width=GetSystemMetrics(SM_CXVIRTUALSCREEN),height=GetSystemMetrics(SM_CYVIRTUALSCREEN);
            if(width<2||height<2)return false;
            INPUT inputs[3]{};for(auto& input:inputs){input.type=INPUT_MOUSE;input.mi.dwExtraInfo=0x594d5047u;}
            inputs[0].mi.dx=MulDiv(point.x-x,65535,width-1);inputs[0].mi.dy=MulDiv(point.y-y,65535,height-1);
            inputs[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;
            inputs[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;inputs[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
            if(!owns(WindowFromPoint(point))||SendInput(3,inputs,sizeof(INPUT))!=3)return false;
            bootstrapUsed=true;pump(80);if(owns(GetForegroundWindow()))return true;
        }
        return false;
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
        (h==windows[0]||h==outlines[0]||h==windows[1]||h==outlines[1]||IsChild(windows[0],h)));
}
static bool mouseClickAndWheel(const OwnTarget& target,POINT point) {
    if(!authorizedPoint(target,point)||!target.owns(GetForegroundWindow()))return false;
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
