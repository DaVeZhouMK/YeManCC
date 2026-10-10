// OS experiment only: production touchpad HWND + a target on a DIFFERENT UI thread.
// No driver, real config, key output, forwarding click, UIAccess or global input hook.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <fstream>
#include <thread>
#include <atomic>
using namespace ymcc::screenpads;
static HWND target=nullptr;static WNDPROC original=nullptr;
static std::atomic<unsigned> mouseDowns{0},targetTouches{0},overlayMouseDowns{0};
static std::atomic<bool> deviceAware{false};static Json hits=Json::array();
static LRESULT CALLBACK targetProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_LBUTTONDOWN)++mouseDowns;if(m==WM_POINTERDOWN)++targetTouches;
 if(m==WM_CLOSE){DestroyWindow(h);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK observedProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_NCHITTEST){INPUT_MESSAGE_SOURCE source{};const bool known=GetCurrentInputMessageSource(&source)!=FALSE;
  const ULONG_PTR extra=static_cast<ULONG_PTR>(GetMessageExtraInfo());
  const bool touchMarker=(extra&0xffffff00u)==0xff515700u&&(extra&0x80u)!=0;
  hits.push_back({{"device",source.deviceType},{"origin",source.originId},{"known",known},{"touchMarker",touchMarker},{"experimental",deviceAware.load()}});
  if(deviceAware.load())return HTTRANSPARENT; // Deliberate test-only unconditional transparency, never a production change.
 }
 if(m==WM_LBUTTONDOWN)++overlayMouseDowns;
 return CallWindowProcW(original,h,m,w,l);
}
static void pump(DWORD ms){const auto end=GetTickCount64()+ms;MSG m{};do{while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))DispatchMessageW(&m);Sleep(1);}while(GetTickCount64()<end);}
static bool click(POINT p){INPUT data[3]{};data[0].type=data[1].type=data[2].type=INPUT_MOUSE;
 data[0].mi.dx=MulDiv(p.x,65535,GetSystemMetrics(SM_CXSCREEN)-1);data[0].mi.dy=MulDiv(p.y,65535,GetSystemMetrics(SM_CYSCREEN)-1);data[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE;
 data[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;data[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
 return SendInput(3,data,sizeof(INPUT))==3;
}
static bool touch(POINT p){POINTER_TOUCH_INFO info{};info.pointerInfo.pointerType=PT_TOUCH;info.pointerInfo.pointerId=0;info.pointerInfo.ptPixelLocation=p;info.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;info.rcContact={p.x-2,p.y-2,p.x+2,p.y+2};info.pressure=512;info.orientation=90;
 info.pointerInfo.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;const bool down=InjectTouchInput(1,&info)!=FALSE;pump(30);
 info.pointerInfo.pointerFlags=POINTER_FLAG_UP;const bool up=InjectTouchInput(1,&info)!=FALSE;pump(60);return down&&up;
}
int main(int argc,char**argv){if(argc!=2)return 2;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);POINT before{};GetCursorPos(&before);const HWND prior=GetForegroundWindow();HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,nullptr);
 std::thread thread([&]{WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"YmccOwnMousePassTarget";c.lpfnWndProc=targetProc;c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=(HBRUSH)(COLOR_WINDOW+1);RegisterClassW(&c);target=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,c.lpszClassName,L"YMCC 自有输入验收窗口",WS_POPUP|WS_VISIBLE,300,220,500,340,nullptr,nullptr,c.hInstance,nullptr);SetEvent(ready);MSG m{};while(GetMessageW(&m,nullptr,0,0)>0)DispatchMessageW(&m);});
 WaitForSingleObject(ready,3000);Json result={{"actualWindowsInput",true},{"targetDifferentThread",true},{"clickForwarded",false},{"physicalHardwareWrites",0},{"productionInputChanged",false}};
 Config cfg;cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;dryRun=true;initialize(GetModuleHandleW(nullptr),nullptr,cfg);SetWindowPos(windows[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);SetWindowPos(outlines[0],HWND_TOPMOST,360,280,260,200,SWP_NOACTIVATE|SWP_SHOWWINDOW);sizes[0]={260,200};original=(WNDPROC)SetWindowLongPtrW(windows[0],GWLP_WNDPROC,(LONG_PTR)observedProc);pump(50);POINT p{450,350};
 const unsigned first=mouseDowns;const auto ev=eventCount;click(p);pump(70);const bool touchReady=InitializeTouchInjection(2,TOUCH_FEEDBACK_NONE)!=FALSE;const bool baselineTouch=touchReady&&touch(p);
 result["baseline"]={{"targetMouseClicks",mouseDowns-first},{"overlayMouseClicks",overlayMouseDowns.load()},{"touchInjected",baselineTouch},{"touchpadPointerEvents",eventCount-ev}};
 deviceAware=true;const unsigned second=mouseDowns;const auto ev2=eventCount;const auto targetTouchBefore=targetTouches.load();click(p);pump(70);const bool experimentalTouch=touchReady&&touch(p);
 result["experiment"]={{"targetMouseClicks",mouseDowns-second},{"touchInjected",experimentalTouch},{"touchpadPointerEvents",eventCount-ev2},{"underlyingTouchEvents",targetTouches-targetTouchBefore}};result["hitSources"]=hits;
 result["experimentPreservesBoth"]=mouseDowns>second&&eventCount>ev2&&targetTouches==targetTouchBefore;
 SetWindowLongPtrW(windows[0],GWLP_WNDPROC,(LONG_PTR)original);shutdown();PostMessageW(target,WM_CLOSE,0,0);thread.join();CloseHandle(ready);SetCursorPos(before.x,before.y);if(prior&&IsWindow(prior))SetForegroundWindow(prior);
 std::ofstream out(argv[1],std::ios::binary);out<<result.dump(2);return 0;
}
