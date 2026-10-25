// Bounded active verification against the installed HUD. Touches are restricted
// to our temporary receiver; verifies Windows injected routing, not physical hardware.
#include <windows.h>
#include <cstdio>
#include <string>
#include <vector>
#include <fstream>
#include "json.hpp"
static std::vector<nlohmann::json> received;
static LRESULT CALLBACK proc(HWND h,UINT msg,WPARAM w,LPARAM l){
 if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
 if(msg==WM_POINTERACTIVATE)return PA_NOACTIVATE;
 if(msg==WM_TOUCH){const UINT count=LOWORD(w);TOUCHINPUT input[4]{};if(count<=4&&GetTouchInputInfo(reinterpret_cast<HTOUCHINPUT>(l),count,input,sizeof(TOUCHINPUT))){for(UINT i=0;i<count;++i)received.push_back({{"type","touch"},{"flags",input[i].dwFlags},{"x",input[i].x/100},{"y",input[i].y/100}});}CloseTouchInputHandle(reinterpret_cast<HTOUCHINPUT>(l));return 0;}
 if(msg==WM_POINTERDOWN||msg==WM_POINTERUP)received.push_back({{"type","pointer"},{"msg",msg}});
 if(msg==WM_LBUTTONDOWN||msg==WM_LBUTTONUP)received.push_back({{"type","mouse"},{"msg",msg}});
 return DefWindowProcW(h,msg,w,l);
}
static void pump(DWORD duration){const ULONGLONG end=GetTickCount64()+duration;MSG m{};do{while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}Sleep(5);}while(GetTickCount64()<end);}
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;nlohmann::json report{{"ok",false},{"physicalTouchTest",false},{"scope","Windows injected touch routed through installed HUD into owned temporary receiver"},{"cases",nlohmann::json::array()}};
 HWND receiver=nullptr;HWND previous=GetForegroundWindow();
 try{
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  report["desktop"]={{"x",GetSystemMetrics(SM_XVIRTUALSCREEN)},{"y",GetSystemMetrics(SM_YVIRTUALSCREEN)},{"w",GetSystemMetrics(SM_CXVIRTUALSCREEN)},{"h",GetSystemMetrics(SM_CYVIRTUALSCREEN)}};
  HWND hud=FindWindowW(L"YMCC_DesktopMonitorHUD",L"YMCC Monitor HUD");RECT r{};
  if(!hud||!IsWindowVisible(hud)||!GetWindowRect(hud,&r)||r.right-r.left<120||r.bottom-r.top<100)throw std::string("Visible installed HUD required");
  WNDCLASSEXW cls{sizeof(cls)};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=proc;cls.lpszClassName=L"YMCC_TouchRoutingOwnedReceiver";cls.hbrBackground=reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));RegisterClassExW(&cls);
  receiver=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"YMCC touch test receiver",WS_POPUP,r.left-8,r.top-8,r.right-r.left+16,r.bottom-r.top+16,nullptr,nullptr,cls.hInstance,nullptr);
  if(!receiver||!RegisterTouchWindow(receiver,TWF_WANTPALM))throw std::string("Owned touch receiver creation failed");
  ShowWindow(receiver,SW_SHOWNOACTIVATE);SetWindowPos(receiver,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);SetWindowPos(receiver,hud,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);UpdateWindow(receiver);pump(100);
  if(!InitializeTouchInjection(1,TOUCH_FEEDBACK_NONE))throw std::string("InitializeTouchInjection failed ")+std::to_string(GetLastError());
  const POINT points[]={{r.left+12,r.top+12},{(r.left+r.right)/2,(r.top+r.bottom)/2},{r.right-12,r.bottom-12}};
  for(int i=0;i<3;++i){const POINT point=points[i];const HWND hit=WindowFromPoint(point);if(hit!=receiver&&GetAncestor(hit,GA_ROOT)!=receiver){report["refusedHit"]=reinterpret_cast<ULONG_PTR>(hit);report["hud"]=reinterpret_cast<ULONG_PTR>(hud);report["receiver"]=reinterpret_cast<ULONG_PTR>(receiver);throw std::string("Receiver is not the underlying target; refuse touch injection");}
   report["attemptedPoint"]={{"x",point.x},{"y",point.y}};const size_t start=received.size();POINTER_TOUCH_INFO touch{};touch.pointerInfo.pointerType=PT_TOUCH;touch.pointerInfo.pointerId=0;touch.pointerInfo.ptPixelLocation=point;touch.pointerInfo.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;touch.touchFlags=TOUCH_FLAG_NONE;touch.touchMask=TOUCH_MASK_CONTACTAREA;touch.rcContact={point.x-3,point.y-3,point.x+3,point.y+3};touch.orientation=90;touch.pressure=320;
   if(!InjectTouchInput(1,&touch))throw std::string("Touch down rejected ")+std::to_string(GetLastError());pump(50);touch.pointerInfo.pointerFlags=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;if(!InjectTouchInput(1,&touch))throw std::string("Touch update rejected ")+std::to_string(GetLastError());pump(40);
   touch.pointerInfo.pointerFlags=POINTER_FLAG_UP;if(!InjectTouchInput(1,&touch))throw std::string("Touch up rejected ")+std::to_string(GetLastError());pump(180);
   nlohmann::json events=nlohmann::json::array();bool down=false,up=false;for(size_t k=start;k<received.size();++k){const auto& e=received[k];events.push_back(e);if(e["type"]=="touch"){const auto flags=e["flags"].get<ULONG>();down=down||(flags&TOUCHEVENTF_DOWN);up=up||(flags&TOUCHEVENTF_UP);}}
   report["cases"].push_back({{"x",point.x},{"y",point.y},{"down",down},{"up",up},{"events",events}});if(!down||!up)throw std::string("Installed HUD blocked injected touch from owned receiver");
  }
  report["ok"]=true;report["foregroundUnchanged"]=GetForegroundWindow()==previous;
 }catch(const std::string& e){report["error"]=e;report["win32Error"]=GetLastError();}
 if(receiver){UnregisterTouchWindow(receiver);DestroyWindow(receiver);}
 std::ofstream out(argv[1]);out<<report.dump(2);return report["ok"].get<bool>()?0:1;
}
