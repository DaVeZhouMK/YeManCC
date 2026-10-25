// Native layering fixture on a private desktop; no physical/device input.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "screen_touchpads.h"
#include <iostream>
using namespace ymcc::screenpads;
static HWND fixtureForeground=nullptr;
static HWND fixtureGetForeground(){return fixtureForeground;}
static DWORD fixtureWindowProcess(HWND h,DWORD* pid){
    const DWORD thread=GetWindowThreadProcessId(h,pid);
    // Only the visibility adapter sees a non-self foreground fixture. All HWNDs
    // and all ordering/geometry/style calls are real private-desktop User32.
    if(pid&&h==fixtureForeground)*pid=GetCurrentProcessId()+1;
    return thread;
}
namespace ymcc::screenpads {inline bool onOwnerThread(HWND h){return h&&GetWindowThreadProcessId(h,nullptr)==GetCurrentThreadId();}}
#define GetForegroundWindow fixtureGetForeground
#define GetWindowThreadProcessId fixtureWindowProcess
#include "screen_touchpad_visibility.h"
#undef GetWindowThreadProcessId
#undef GetForegroundWindow
static std::wstring desktopName(HDESK h){wchar_t s[256]{};DWORD bytes=0;return h&&GetUserObjectInformationW(h,UOI_NAME,s,sizeof(s),&bytes)?s:L"";}
static std::wstring inputName(){HDESK h=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);auto s=desktopName(h);if(h)CloseDesktop(h);return s;}
static void drain(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE))DispatchMessageW(&m);}
static bool above(HWND higher,HWND lower){for(HWND h=GetWindow(lower,GW_HWNDPREV);h;h=GetWindow(h,GW_HWNDPREV))if(h==higher)return true;return false;}
static Json order(){Json result=Json::array();for(HWND h=GetTopWindow(nullptr);h;h=GetWindow(h,GW_HWNDNEXT)){
    if(h==windows[0])result.push_back("input");else if(h==outlines[0])result.push_back("outline");
    else if(h==contactVisuals[0].window)result.push_back("contact");else if(h==cursorWindow)result.push_back("cursor");
    else if(h==fixtureForeground)result.push_back("external");
}return result;}
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;const std::filesystem::path path(argv[1]);Json cases=Json::array(),snapshots=Json::array();bool ok=true;
 auto check=[&](const char* n,bool pass){cases.push_back({{"name",n},{"ok",pass}});ok&=pass;};
 const auto before=inputName();const HDESK original=GetThreadDesktop(GetCurrentThreadId());
 const std::wstring name=L"YMCC_TouchpadStack_"+std::to_wstring(GetCurrentProcessId());
 HDESK desk=CreateDesktopW(name.c_str(),nullptr,nullptr,0,DESKTOP_CREATEWINDOW|DESKTOP_READOBJECTS|DESKTOP_WRITEOBJECTS,nullptr);
 if(!desk||!SetThreadDesktop(desk)){std::ofstream(path)<<Json{{"ok",false},{"stage","private-desktop"},{"error",GetLastError()}}.dump(2);if(desk)CloseDesktop(desk);return 4;}
 HWND host=nullptr;unsigned long long uploadsAtStart=0,outlineAtStart=0,cursorAtStart=0,raisesAtStart=0;
 try{
  instance=GetModuleHandleW(nullptr);dryRun=true;
  WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=DefWindowProcW;wc.lpszClassName=L"YMCC_TouchpadStack_Owned";
  if(!RegisterClassW(&wc))throw std::runtime_error("fixture class registration failed");
  host=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"",WS_POPUP|WS_VISIBLE,0,0,800,600,nullptr,nullptr,instance,nullptr);
  owner=host;initialized=true;
  Config cfg;cfg.enabled=true;cfg.left=Mode::Deck;cfg.right=Mode::Off;cfg.touchFeedback=TouchFeedbackMode::Off;cfg.autoHideSeconds=0;
  core.config=cfg;available=true;sizes[0]={400,300};core.pads[0].down=true;core.pads[0].x=200;core.pads[0].y=150;
  windows[0]=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP|WS_VISIBLE|WS_DISABLED,100,100,400,300,nullptr,nullptr,instance,nullptr);
  WNDCLASSW oc{};oc.hInstance=instance;oc.lpfnWndProc=outlineProc;oc.lpszClassName=L"YMCC_TouchpadStack_Outline";
  if(!RegisterClassW(&oc))throw std::runtime_error("outline class registration failed");
  outlines[0]=CreateWindowExW(WS_EX_TOPMOST|ymcc::screeninput::displayExStyle,oc.lpszClassName,L"",WS_POPUP|WS_VISIBLE|WS_DISABLED,100,100,400,300,windows[0],nullptr,instance,nullptr);
  if(!host||!windows[0]||!outlines[0]||!renderOutline(0,cfg.transparency,true))throw std::runtime_error("pad setup failed");
  syncContactVisuals(0,false);
  cursorTracking=true;cursorDeckOnlyTracking=true;
  CURSORINFO ci{sizeof(ci),CURSOR_SUPPRESSED,LoadCursorW(nullptr,IDC_ARROW),{300,250}};cursorSyncInfo(ci,1000,false);drain();
  check("initial-feedback-stack-ordered",above(contactVisuals[0].window,outlines[0])&&above(cursorWindow,contactVisuals[0].window));
  check("initial-visual-layers-inert",ymcc::screeninput::displayInert(outlines[0])&&ymcc::screeninput::displayInert(contactVisuals[0].window)&&ymcc::screeninput::displayInert(cursorWindow));
  snapshots.push_back({{"stage","initial"},{"order",order()}});
  uploadsAtStart=contactVisualUploads;outlineAtStart=outlineUploadCount;cursorAtStart=cursorUploads;
  fixtureForeground=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"",WS_POPUP|WS_VISIBLE,0,0,800,600,nullptr,nullptr,instance,nullptr);
  if(!fixtureForeground)throw std::runtime_error("external fixture setup failed");
  SetWindowPos(fixtureForeground,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);drain();
  check("external-topmost-fixture-occludes-pad",screenLayerOccluded(outlines[0],fixtureForeground));
  snapshots.push_back({{"stage","before-visibility-event"},{"order",order()}});
  raisesAtStart=ownerVisibilityRaises;ownerVisibilityQueued=true;
  const auto eventQueries=cursorQueries;const auto eventActiveWindow=GetActiveWindow();
  RECT inputRect{},contactRect{},cursorRect{};GetWindowRect(windows[0],&inputRect);GetWindowRect(contactVisuals[0].window,&contactRect);GetWindowRect(cursorWindow,&cursorRect);
  check("existing-visibility-dispatch-accepted",consumeOwnerVisibility(ownerVisibilityEpoch));drain();
  RECT inputAfter{},contactAfter{},cursorAfter{};GetWindowRect(windows[0],&inputAfter);GetWindowRect(contactVisuals[0].window,&contactAfter);GetWindowRect(cursorWindow,&cursorAfter);
  check("visibility-recovery-preserves-geometry",EqualRect(&inputRect,&inputAfter)&&EqualRect(&contactRect,&contactAfter)&&EqualRect(&cursorRect,&cursorAfter));
  check("visibility-recovery-preserves-activation-and-contact",GetActiveWindow()==eventActiveWindow&&core.pads[0].down&&core.pads[0].x==200&&core.pads[0].y==150&&cursorTracking&&!cursorRetained);
  check("visibility-recovery-makes-no-cursor-query",cursorQueries==eventQueries);
  snapshots.push_back({{"stage","after-visibility-event"},{"order",order()}});
  check("pad-pair-restored-above-external",above(windows[0],fixtureForeground)&&above(outlines[0],fixtureForeground)&&ownerVisibilityRaises>raisesAtStart);
  {const HWND hit=WindowFromPoint(ci.ptScreenPos);Json hitInfo=Json::array();
   for(HWND h:{windows[0],outlines[0],contactVisuals[0].window,cursorWindow,fixtureForeground,host,hit}){
    wchar_t cls[128]{};GetClassNameW(h,cls,128);RECT r{};GetWindowRect(h,&r);
    hitInfo.push_back({{"hwnd",(ULONG_PTR)h},{"visible",IsWindowVisible(h)!=FALSE},{"enabled",IsWindowEnabled(h)!=FALSE},{"style",GetWindowLongPtrW(h,GWL_STYLE)},
     {"exStyle",GetWindowLongPtrW(h,GWL_EXSTYLE)},{"rect",{r.left,r.top,r.right,r.bottom}}});
   }snapshots.push_back({{"stage","native-hit-diagnostic"},{"hit",(ULONG_PTR)hit},{"expectedSurrogateInput",(ULONG_PTR)windows[0]},{"windows",hitInfo}});
   // The surrogate input HWND is deliberately opaque and has no WinRT
   // controlled-input bridge. Here only assert the display layers are skipped;
   // lower-process mouse pass is covered by the full native-input fixture.
   check("native-hit-bypasses-feedback-to-surrogate-input",hit==windows[0]);
  }
  check("contact-restored-above-outline-and-external",above(contactVisuals[0].window,outlines[0])&&above(contactVisuals[0].window,fixtureForeground));
  check("cursor-restored-above-contact-and-external",above(cursorWindow,contactVisuals[0].window)&&above(cursorWindow,fixtureForeground));
  const auto raisesAfterEvent=ownerVisibilityRaises;
  for(int i=0;i<100;++i){core.pads[0].x=170+(i%60);core.pads[0].y=140+(i%20);syncContactVisuals(0,false);cursorSyncInfo(ci,1024+i*24ull,false);}
  snapshots.push_back({{"stage","after-stationary-cursor-moving-finger"},{"order",order()}});
  check("moving-contact-retains-order-with-stationary-cursor",above(contactVisuals[0].window,outlines[0])&&above(contactVisuals[0].window,fixtureForeground));
  check("stable-moves-do-not-rebuild-visual-bitmaps",contactVisualUploads==uploadsAtStart&&outlineUploadCount==outlineAtStart&&cursorUploads==cursorAtStart);
  check("stable-moves-do-not-issue-visibility-stack-raises",ownerVisibilityRaises==raisesAfterEvent);
  check("all-display-windows-still-inert",ymcc::screeninput::displayInert(outlines[0])&&ymcc::screeninput::displayInert(contactVisuals[0].window)&&ymcc::screeninput::displayInert(cursorWindow));
  ownerVisibilityQueued=true;const auto beforeNoop=ownerVisibilityRaises;consumeOwnerVisibility(ownerVisibilityEpoch);
  check("already-recovered-event-does-not-repeat-raises",ownerVisibilityRaises==beforeNoop);
  check("stale-event-does-not-change-stack",!consumeOwnerVisibility(ownerVisibilityEpoch-1)&&ownerVisibilityRaises==beforeNoop);
  // Retained cursor has no active pad contact. Recovery must not depend on
  // continued touch movement or drop retained ownership while repairing order.
  core.pads[0].down=false;syncContactVisuals(0,false);cursorTracking=false;cursorRetained=true;
  SetWindowPos(fixtureForeground,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);ownerVisibilityQueued=true;consumeOwnerVisibility(ownerVisibilityEpoch);drain();
  snapshots.push_back({{"stage","retained-after-visibility-event"},{"order",order()}});
  check("retained-cursor-restored-above-external",above(cursorWindow,fixtureForeground)&&cursorVisible&&cursorRetained&&!cursorTracking);
  check("hidden-contact-is-not-reshown",!contactVisuals[0].visible&&!IsWindowVisible(contactVisuals[0].window));
  // An explicit cursor hide remains authoritative across another real pair
  // restore. The display repair never uses SWP_SHOWWINDOW on these layers.
  ci.flags=0;cursorSyncInfo(ci,5000,false);
  SetWindowPos(fixtureForeground,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);ownerVisibilityQueued=true;consumeOwnerVisibility(ownerVisibilityEpoch);drain();
  check("explicitly-hidden-cursor-is-not-reshown",!cursorVisible&&!IsWindowVisible(cursorWindow)&&!cursorRetained);
  check("recovery-events-never-rebuild-bitmaps",contactVisualUploads==uploadsAtStart&&outlineUploadCount==outlineAtStart&&cursorUploads==cursorAtStart);
  check("fixture-produced-no-real-input",injectedCount==0&&sentKeys==0&&!sentClick);
 }catch(const std::exception& e){ok=false;cases.push_back({{"name","fixture-exception"},{"ok",false},{"error",e.what()},{"lastError",GetLastError()}});}
 cursorShutdown();shutdownContactVisuals();core.pads[0].down=false;
 if(fixtureForeground)DestroyWindow(fixtureForeground);fixtureForeground=nullptr;
 if(outlines[0])DestroyWindow(outlines[0]);outlines[0]=nullptr;outlineUploads={};for(auto& image:outlineBitmaps)image.reset();for(auto& pair:outlineFrames)for(auto& frame:pair)frame.reset();
 if(windows[0])DestroyWindow(windows[0]);windows[0]=nullptr;if(host)DestroyWindow(host);owner=nullptr;initialized=false;drain();
 check("restored-original-thread-desktop",SetThreadDesktop(original)!=FALSE);check("closed-private-desktop",CloseDesktop(desk)!=FALSE);
 check("input-desktop-never-switched",!before.empty()&&before==inputName());
 Json result={{"ok",ok},{"cases",cases},{"snapshots",snapshots},{"scope","Native private-desktop z-order; substituted foreground adapter; no Steam or scanout proof"},
 {"hiddenDesktop",true},{"inputDesktopUnchanged",before==inputName()},{"systemInputInjected",false},{"systemCursorManipulated",false},{"visibleDesktopSwitched",false},{"productDeployed",false},{"fieldFlickerVerified",false}};
 std::ofstream out(path);out<<result.dump(2);std::cout<<"NATIVE_STACK ok="<<ok<<" checks="<<cases.size()<<"\n";return ok&&out.good()?0:3;
}
