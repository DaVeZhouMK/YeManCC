// Native User32/GDI surface verification on a private never-switched desktop.
// Cursor observations are fixtures passed to production cursorSyncInfo; this
// program never injects input, warps/shows the system cursor or launches Steam.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
using namespace ymcc::screenpads;
static std::wstring desktopName(HDESK desktop){wchar_t name[256]{};DWORD bytes=0;return desktop&&GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&bytes)?name:L"";}
static std::wstring inputDesktopName(){const HDESK h=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);const auto name=desktopName(h);if(h)CloseDesktop(h);return name;}
static void drain(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;const auto path=std::filesystem::path(argv[1]);Json cases=Json::array();bool ok=true;
 auto check=[&](const char* name,bool pass){cases.push_back({{"name",name},{"ok",pass}});ok&=pass;};
 const auto initialInput=inputDesktopName();const HDESK original=GetThreadDesktop(GetCurrentThreadId());
 const std::wstring name=L"YMCC_CursorSurface_R4_"+std::to_wstring(GetCurrentProcessId());
 HDESK desk=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_CREATEMENU|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS,nullptr);
 if(!desk||!SetThreadDesktop(desk)){const DWORD error=GetLastError();if(desk)CloseDesktop(desk);std::ofstream out(path);out<<Json{{"ok",false},{"blocked",true},{"error",error},{"stage","private-desktop"}}.dump(2);return 4;}
 instance=GetModuleHandleW(nullptr);dryRun=true;const DWORD coldGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);DWORD gdiBefore=0;
 HWND lower=nullptr;unsigned long long hotUploads=0,hotMoves=0;DWORD gdiAfter=0;Json diagnostics=Json::array();
 auto diagnostic=[&](const char* label){diagnostics.push_back({{"label",label},{"error",cursorError},{"visible",cursorVisible},{"uploaded",cursorImageUploaded},{"window",(ULONG_PTR)cursorWindow},{"shape",(ULONG_PTR)cursorShape},{"exStyle",cursorWindow?GetWindowLongPtrW(cursorWindow,GWL_EXSTYLE):0},{"style",cursorWindow?GetWindowLongPtrW(cursorWindow,GWL_STYLE):0},{"gdi",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)}});};
 try {
  check("private-desktop-is-not-input-desktop",!initialInput.empty()&&desktopName(desk)!=initialInput);
  WNDCLASSW wc{};wc.hInstance=instance;wc.lpszClassName=L"YMCC_CursorSurface_R4_Lower";wc.lpfnWndProc=DefWindowProcW;
  if(!RegisterClassW(&wc))throw std::runtime_error("class creation failed");
  lower=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Owned native hit target",WS_POPUP|WS_VISIBLE,0,0,1000,800,nullptr,nullptr,instance,nullptr);
  if(!lower)throw std::runtime_error("lower window creation failed");
  HCURSOR arrow=LoadCursorW(nullptr,IDC_ARROW);if(!arrow)throw std::runtime_error("arrow unavailable");
  // Warm the shared system cursor/GDI caches before measuring owned retention.
  // Cold process counts include one-time shared cursor resources, not a leak.
  cursorTracking=true;cursorDeckOnlyTracking=true;CURSORINFO warm{sizeof(warm),CURSOR_SUPPRESSED,arrow,{200,180}};
  for(const auto shape:{IDC_ARROW,IDC_HAND,IDC_IBEAM,IDC_CROSS}){warm.hCursor=LoadCursorW(nullptr,shape);cursorSyncInfo(warm,1,false);}
  cursorShutdown();drain();gdiBefore=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
  cursorUploads=cursorShows=cursorHides=cursorDeckPositionOnlyMoves=cursorDeckPositionFallbacks=0;
  cursorError=cursorDeckPositionLastError=0;
  cursorTracking=true;cursorDeckOnlyTracking=true;CURSORINFO ci{sizeof(ci),CURSOR_SUPPRESSED,arrow,{200,180}};
  cursorSyncInfo(ci,1000,false);drain();
  check("suppressed-fixture-creates-one-real-layered-surface",cursorWindow&&cursorVisible&&IsWindowVisible(cursorWindow)&&cursorImageUploaded&&cursorUploads==1);
  check("cursor-surface-disabled-transparent-and-nonactivating",ymcc::screeninput::displayInert(cursorWindow));
  const auto uploads=cursorUploads,shows=cursorShows,hides=cursorHides,moves=cursorDeckPositionOnlyMoves;bool aligned=true,passThrough=true;
  for(int i=0;i<1000;++i){ci.ptScreenPos={200+i%350,180+(i%200)};cursorSyncInfo(ci,1024+i*24ull,false);RECT r{};
   aligned&=GetWindowRect(cursorWindow,&r)&&r.left+cursorHotspot.x==ci.ptScreenPos.x&&r.top+cursorHotspot.y==ci.ptScreenPos.y;
   passThrough&=WindowFromPoint(ci.ptScreenPos)==lower;
  }
  hotUploads=cursorUploads-uploads;hotMoves=cursorDeckPositionOnlyMoves-moves;
  check("1000-native-positions-preserve-cursor-hotspot",aligned);
  check("1000-native-hit-tests-reach-owned-window-under-cursor",passThrough);
  check("steady-native-moves-reuse-bitmap-without-show-hide",cursorUploads==uploads&&cursorShows==shows&&cursorHides==hides);
  check("native-position-only-API-works-without-fallback",hotMoves>0&&cursorDeckPositionFallbacks==0&&cursorDeckPositionLastError==0);
  ci.flags=CURSOR_SHOWING;cursorSyncInfo(ci,26000,false);check("real-layered-window-hides-on-system-owner-fixture",!cursorVisible&&!IsWindowVisible(cursorWindow));
  ci.hCursor=nullptr;const auto h=cursorHides,s=cursorShows;cursorSyncInfo(ci,26024,false);
  check("null-SHOWING-fixture-does-not-reshow-real-window",!cursorVisible&&!IsWindowVisible(cursorWindow)&&cursorHides==h&&cursorShows==s);
  ci.flags=CURSOR_SUPPRESSED;cursorSyncInfo(ci,26048,false);check("suppressed-null-fixture-reuses-existing-native-image",cursorVisible&&cursorUploads==uploads&&cursorImageUploaded);
  diagnostic("before-enable");EnableWindow(cursorWindow,TRUE);diagnostic("after-enable");
  check("WM_ENABLE-restores-native-noninteractive-policy",!IsWindowEnabled(cursorWindow)&&ymcc::screeninput::displayInert(cursorWindow));
  const auto style=GetWindowLongPtrW(cursorWindow,GWL_EXSTYLE);SetWindowLongPtrW(cursorWindow,GWL_EXSTYLE,style&~(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE));
  diagnostic("after-style-repair");
  check("style-change-repairs-policy-and-invalidates-upload",ymcc::screeninput::displayInert(cursorWindow)&&!cursorImageUploaded&&!cursorVisible);
  ci.hCursor=arrow;cursorSyncInfo(ci,26100,false);diagnostic("after-next-upload");
  check("next-suppressed-frame-recovers-native-surface",cursorVisible&&cursorImageUploaded&&cursorUploads==uploads+1);
  bool shapeSafe=true;
  const LPCWSTR shapes[]={IDC_ARROW,IDC_HAND,IDC_IBEAM,IDC_CROSS};
  for(int i=0;i<128;++i){ci.hCursor=LoadCursorW(nullptr,shapes[i%4]);cursorSyncInfo(ci,26200+i*24ull,false);shapeSafe&=cursorVisible&&cursorShape==ci.hCursor&&cursorImageUploaded&&ymcc::screeninput::displayInert(cursorWindow);}
  diagnostic("after-shape-loop");check("128-native-shape-changes-retain-valid-inert-surface",shapeSafe);
  // A second equal shape cycle distinguishes one-time GDI caches from growth.
  const DWORD repeatedShapeGdi=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
  for(int i=0;i<128;++i){ci.hCursor=LoadCursorW(nullptr,shapes[i%4]);cursorSyncInfo(ci,30200+i*24ull,false);}
  check("repeated-native-shape-cycle-does-not-grow-GDI-retention",GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=repeatedShapeGdi+2);
  ci.flags=0;cursorSyncInfo(ci,30000,false);check("explicit-hide-fixture-keeps-native-window-hidden",!cursorVisible&&!IsWindowVisible(cursorWindow));
  check("native-render-path-reported-no-error",cursorError==0);
 }catch(const std::exception& e){ok=false;cases.push_back({{"name","unexpected-native-error"},{"ok",false},{"error",e.what()}});}
 cursorShutdown();drain();if(lower)DestroyWindow(lower);drain();gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 check("shutdown-destroys-cursor-and-clears-owned-state",!cursorWindow&&!cursorTracking&&!cursorRetained);
 // Cold Win32/GDI startup retains process caches. Compare repeated complete
 // surface lifetimes after the same paths have run, rather than cold zero.
 const DWORD steadyGdi=gdiAfter;bool boundedResources=true,repeatedRecovery=true;
 for(int cycle=0;cycle<12;++cycle){
  cursorTracking=true;cursorDeckOnlyTracking=true;CURSORINFO repeat{sizeof(repeat),CURSOR_SUPPRESSED,LoadCursorW(nullptr,IDC_ARROW),{200,180}};
  cursorSyncInfo(repeat,40000+cycle*1000ull,false);
  for(const auto shape:{IDC_ARROW,IDC_HAND,IDC_IBEAM,IDC_CROSS}){repeat.hCursor=LoadCursorW(nullptr,shape);cursorSyncInfo(repeat,40100+cycle*1000ull,false);}
  SetWindowLongPtrW(cursorWindow,GWL_EXSTYLE,GetWindowLongPtrW(cursorWindow,GWL_EXSTYLE)&~WS_EX_LAYERED);
  cursorSyncInfo(repeat,40200+cycle*1000ull,false);
  repeatedRecovery&=cursorVisible&&cursorImageUploaded&&ymcc::screeninput::displayInert(cursorWindow);
  cursorShutdown();drain();boundedResources&=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS)<=steadyGdi+2;
 }
 gdiAfter=GetGuiResources(GetCurrentProcess(),GR_GDIOBJECTS);
 check("12-native-style-losses-recover-before-shutdown",repeatedRecovery);
 check("12-complete-native-surface-lifetimes-do-not-grow-GDI-retention",boundedResources);
 check("final-resource-cycle-clears-owned-window",!cursorWindow&&!cursorTracking&&!cursorRetained);
 check("test-never-injected-keys-mouse-or-device-output",injectedCount==0&&sentKeys==0&&!sentClick);
 const bool restored=SetThreadDesktop(original)!=FALSE;check("thread-restored-to-original-desktop",restored);
 const bool closed=CloseDesktop(desk)!=FALSE;check("private-desktop-handle-closed",closed);
 check("visible-input-desktop-remains-original",!initialInput.empty()&&inputDesktopName()==initialInput);
 Json result={{"ok",ok},{"cases",cases},{"diagnostics",diagnostics},{"nativeUser32Gdi",true},{"fixtureCursorInfo",true},{"hiddenDesktop",true},{"inputDesktopUnchanged",inputDesktopName()==initialInput},{"visibleDesktopSwitched",false},{"systemInputInjected",false},{"systemCursorManipulated",false},{"hardwareWrites",0},{"productDeployed",false},{"actualSteamOrGame",false},{"dwmScanoutVerified",false},{"fieldFlickerVerified",false},{"coldGdi",coldGdi},{"steadyGdi",steadyGdi},{"gdiBefore",gdiBefore},{"gdiAfter",gdiAfter},{"steadyMoveBitmapUploads",hotUploads},{"steadyPositionOnlyCalls",hotMoves}};
 std::ofstream out(path);out<<result.dump(2);std::cout<<"NATIVE_CURSOR_SURFACE ok="<<ok<<" checks="<<cases.size()<<" uploadsDuringMoves="<<hotUploads<<"\n";return ok&&out.good()?0:3;
}
