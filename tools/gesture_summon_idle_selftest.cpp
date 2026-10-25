#define UNICODE
#define _UNICODE
#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include "gesture_summon.h"
#include "json.hpp"
#include <iostream>
using nlohmann::json;
static unsigned actionMessages=0;
static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==ymcc::gesturesummon::kSummonMessage){++actionMessages;return 0;}return DefWindowProcW(h,m,w,l);}
static unsigned waitMessages(DWORD duration){unsigned n=0;const auto end=GetTickCount64()+duration;while(true){const auto now=GetTickCount64();if(now>=end)break;const DWORD remaining=(DWORD)(end-now);MsgWaitForMultipleObjectsEx(0,nullptr,remaining,QS_ALLINPUT,MWMO_INPUTAVAILABLE);MSG msg{};while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){++n;TranslateMessage(&msg);DispatchMessageW(&msg);}}return n;}
static uint64_t cpu(){FILETIME a{},b{},k{},u{};GetProcessTimes(GetCurrentProcess(),&a,&b,&k,&u);return ((uint64_t)k.dwHighDateTime<<32)+k.dwLowDateTime+((uint64_t)u.dwHighDateTime<<32)+u.dwLowDateTime;}
int main(){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::single_threaded);WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"YmccGestureIdleTest";c.lpfnWndProc=proc;RegisterClassW(&c);HWND owner=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,c.lpszClassName,L"",WS_POPUP,100,100,200,200,nullptr,nullptr,c.hInstance,nullptr);json report={{"scope","isolated test process CPU, excludes DWM/system power and full YMCC"},{"inputInjected",false},{"physicalTouch",false},{"cases",json::array()}};ymcc::gesturesummon::EdgeSurface all;bool ok=owner!=nullptr;for(const bool enabled:{false,true}){ok=all.refresh(owner,enabled,false,true)&&ok;waitMessages(400);const auto before=cpu(),start=GetTickCount64();const auto messages=waitMessages(15000);const auto elapsed=GetTickCount64()-start;const double cpuMs=(cpu()-before)/10000.0;report["cases"].push_back({{"summonEnabled",enabled},{"elapsedMs",elapsed},{"processCpuMs",cpuMs},{"percentOfOneCore",cpuMs/elapsed*100},{"dispatchedMessages",messages},{"actionMessagesTotal",actionMessages}});}all.shutdown();DestroyWindow(owner);report["ok"]=ok&&actionMessages==0;std::cout<<report.dump(2);return report["ok"].get<bool>()?0:1;}
