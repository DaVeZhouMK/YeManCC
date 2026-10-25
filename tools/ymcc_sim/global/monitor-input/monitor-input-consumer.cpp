#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static bool pointerMetadataEnabled=false;
static POINTER_INFO pointerMetadata{};
static BOOL fixtureGetPointerInfo(UINT32 id,POINTER_INFO* value){
 if(pointerMetadataEnabled&&id==pointerMetadata.pointerId){*value=pointerMetadata;return TRUE;}
 return GetPointerInfo(id,value);
}
#define GetPointerInfo fixtureGetPointerInfo
#include "monitor_desktop_hud.test.h"
#undef GetPointerInfo
#include <fstream>
#include <iostream>
#include <vector>
using namespace ymcc;using namespace ymcc::desktophud;
// COM boundary recorder only; production Carrier/Win32/input methods remain unchanged.
class NoticeControllerRecorder final : public Microsoft::WRL::RuntimeClass<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>,ICoreWebView2Controller> {
public:
 BOOL visible=TRUE,closed=FALSE,visibleAtClose=TRUE;RECT rect{};
 HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL *isVisible) override {*isVisible=visible;return S_OK;}
 HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL isVisible) override {visible=isVisible;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_Bounds(RECT *bounds) override {*bounds=rect;return S_OK;}
 HRESULT STDMETHODCALLTYPE put_Bounds(RECT bounds) override {rect=bounds;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_ZoomFactor(double *zoomFactor) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE put_ZoomFactor(double zoomFactor) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(ICoreWebView2ZoomFactorChangedEventHandler *eventHandler, EventRegistrationToken *token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT bounds, double zoomFactor) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON reason) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(ICoreWebView2MoveFocusRequestedEventHandler *eventHandler, EventRegistrationToken *token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE add_GotFocus(ICoreWebView2FocusChangedEventHandler *eventHandler, EventRegistrationToken *token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE add_LostFocus(ICoreWebView2FocusChangedEventHandler *eventHandler, EventRegistrationToken *token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler *eventHandler, EventRegistrationToken *token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken token) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND *parentWindow) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND parentWindow) override {return E_NOTIMPL;}
 HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged() override {return S_OK;}
 HRESULT STDMETHODCALLTYPE Close() override {visibleAtClose=visible;closed=true;return S_OK;}
 HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2 **coreWebView2) override {return E_NOTIMPL;}
};
static unsigned clicks=0,wheels=0,mouseDrags=0;static HWND lower=nullptr;static bool desktopInput=false;static POINT injected{};static bool injectedKnown=false,mouseHeld=false,touchHeld=false,lastWasTouch=false;static POINT touchInjected{};static unsigned injectedPackets=0;static Json lowerPointers=Json::array();static Carrier* pumpingCarrier=nullptr;static unsigned downEvents=0,upEvents=0;
static LRESULT CALLBACK lowerProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_POINTERDOWN||m==WM_POINTERUPDATE||m==WM_POINTERUP){POINTER_INFO p{};if(GetPointerInfo(GET_POINTERID_WPARAM(w),&p))lowerPointers.push_back({{"msg",m},{"type",(int)p.pointerType},{"x",p.ptPixelLocation.x},{"y",p.ptPixelLocation.y}});}
 if(m==WM_LBUTTONDOWN){++clicks;return 0;}if(m==WM_MOUSEWHEEL){++wheels;return 0;}if(m==WM_MOUSEMOVE&&(w&MK_LBUTTON)){++mouseDrags;return 0;}
 if(m==WM_PAINT){PAINTSTRUCT p;HDC dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)GetStockObject(DKGRAY_BRUSH));SetTextColor(dc,RGB(230,240,250));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,L"YMCC input verification - closes automatically",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);EndPaint(h,&p);return 0;}
 return DefWindowProcW(h,m,w,l);
}
static void pump(Carrier& owner,int ms=25){const ULONGLONG end=GetTickCount64()+ms;do{MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}if(owner.current&&owner.current->input.source())owner.current->input.source().Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);if(GetTickCount64()<end)MsgWaitForMultipleObjectsEx(0,nullptr,2,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}while(GetTickCount64()<end);}
static void mouseAt(int x,int y,DWORD button=0){
 DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);if(GetForegroundWindow()!=lower&&foregroundPid!=GetCurrentProcessId())throw std::runtime_error("Owned test window lost foreground; input stopped");
 POINT current{};GetPhysicalCursorPos(&current);if(injectedKnown&&(current.x!=injected.x||current.y!=injected.y)&&!(lastWasTouch&&current.x==touchInjected.x&&current.y==touchInjected.y))throw std::runtime_error("Human cursor movement detected; input stopped");
 const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN),vw=GetSystemMetrics(SM_CXVIRTUALSCREEN),vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 RECT owned{};GetWindowRect(lower,&owned);if(x<owned.left||x>=owned.right||y<owned.top||y>=owned.bottom)throw std::runtime_error("Synthetic mouse outside owned window rejected");
 INPUT a{};a.type=INPUT_MOUSE;a.mi.dx=(LONG)std::lround((x-vx)*65535.0/std::max(1,vw-1));a.mi.dy=(LONG)std::lround((y-vy)*65535.0/std::max(1,vh-1));a.mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK|button;a.mi.dwExtraInfo=0x594d4343;
 if(button==MOUSEEVENTF_WHEEL)a.mi.mouseData=WHEEL_DELTA;
 if(SendInput(1,&a,sizeof a)!=1)throw std::runtime_error("Owned mouse injection failed");++injectedPackets;if(button&MOUSEEVENTF_LEFTDOWN)mouseHeld=true;if(button&MOUSEEVENTF_LEFTUP)mouseHeld=false;
 GetPhysicalCursorPos(&injected);injectedKnown=true;lastWasTouch=false;
}
static void touchAt(int x,int y,POINTER_FLAGS phase){
 const unsigned beforeDown=downEvents,beforeUp=upEvents;
 DWORD foregroundPid=0;GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);if(GetForegroundWindow()!=lower&&foregroundPid!=GetCurrentProcessId())throw std::runtime_error("Owned test window lost foreground; touch stopped pid="+std::to_string(foregroundPid));RECT owned{};GetWindowRect(lower,&owned);if(x<owned.left||x>=owned.right||y<owned.top||y>=owned.bottom)throw std::runtime_error("Synthetic touch outside owned window rejected");
 POINTER_TOUCH_INFO a{};a.pointerInfo.pointerType=PT_TOUCH;a.pointerInfo.pointerId=1;a.pointerInfo.ptPixelLocation={x,y};a.pointerInfo.pointerFlags=phase;a.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;a.rcContact={x-2,y-2,x+2,y+2};a.orientation=90;a.pressure=512;
 if(!InjectTouchInput(1,&a))throw std::runtime_error("Owned touch injection failed: "+std::to_string(GetLastError()));++injectedPackets;touchInjected={x,y};lastWasTouch=true;if(phase&POINTER_FLAG_DOWN)touchHeld=true;if(phase&POINTER_FLAG_UP)touchHeld=false;
 if(pumpingCarrier)pump(*pumpingCarrier,8);
}
template<class Surface> static bool ordinaryMouseRoute(const Surface& surface){
 if constexpr(requires{surface.usesWindowMouse();surface.controlsMouse();})return surface.usesWindowMouse()&&!surface.controlsMouse();
 else return false;
}
static Json rectangle(const RECT& r){return{{"x",r.left},{"y",r.top},{"width",r.right-r.left},{"height",r.bottom-r.top}};}
int wmain(int argc,wchar_t** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);EnableMouseInPointer(TRUE);
 if(argc!=3)return 2;desktopInput=std::wstring(argv[2])==L"--desktop-input";const auto path=argv[1];Json cases=Json::array(),events=Json::array(),trace=Json::array();bool ok=true;std::string error;auto check=[&](const char* n,bool b,Json detail=Json::object()){ok&=b;cases.push_back({{"name",n},{"ok",b},{"detail",detail}});};
 HDESK originalDesktop=GetThreadDesktop(GetCurrentThreadId()),privateDesk=nullptr;HWND originalForeground=GetForegroundWindow();POINT originalCursor{};GetPhysicalCursorPos(&originalCursor);bool restoreCursor=false;
 if(!desktopInput){privateDesk=CreateDesktopW((L"YMCC-HUD-Input-"+std::to_wstring(GetCurrentProcessId())).c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!privateDesk||!SetThreadDesktop(privateDesk))error="Private desktop unavailable";}
 try {if(!error.empty())throw std::runtime_error(error);if(desktopInput&&(GetAsyncKeyState(VK_LBUTTON)&0x8000||GetAsyncKeyState(VK_RBUTTON)&0x8000))throw std::runtime_error("Mouse button already held; input not started");
  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);winrt::init_apartment(winrt::apartment_type::single_threaded);
  {
   bool monitorHoldEnabled=false;unsigned radialHoldDuration=650,radialSummons=0;screeninput::Runtime runtime;Carrier owner;owner.initialize(nullptr,{[](ICoreWebView2*){return true;},[&](const std::string& name,const Json& value){events.push_back({{"name",name},{"value",value}});},nullptr,false,&runtime,[](bool,bool){},[&](int,int){++radialSummons;},[&]{return monitorHoldEnabled;},[&]{return radialHoldDuration;}});
   WNDCLASSW c{};c.lpfnWndProc=lowerProc;c.lpszClassName=L"YMCCMonitorInputFixtureLower";c.hInstance=GetModuleHandleW(nullptr);RegisterClassW(&c);
   MONITORINFO mi{sizeof mi};GetMonitorInfoW(MonitorFromPoint(originalCursor,MONITOR_DEFAULTTOPRIMARY),&mi);int width=600,height=360,left=mi.rcWork.right-width-30,top=mi.rcWork.bottom-height-30;
   lower=CreateWindowExW(WS_EX_TOPMOST,c.lpszClassName,L"YMCC monitor input test",WS_POPUP,left,top,width,height,nullptr,nullptr,c.hInstance,nullptr);if(!lower)throw std::runtime_error("Owned lower window failed");ShowWindow(lower,SW_SHOWNORMAL);if(desktopInput){SetForegroundWindow(lower);if(GetForegroundWindow()!=lower){
 RECT own{};GetWindowRect(lower,&own);POINT p{own.right-20,own.bottom-20};
 if(GetAncestor(WindowFromPhysicalPoint(p),GA_ROOT)!=lower)throw std::runtime_error("Owned activation target occluded; no injection performed");
 const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN),vw=GetSystemMetrics(SM_CXVIRTUALSCREEN),vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 INPUT batch[3]{};for(auto& a:batch){a.type=INPUT_MOUSE;a.mi.dwExtraInfo=0x594d4343;}
 batch[0].mi.dx=(LONG)std::lround((p.x-vx)*65535.0/std::max(1,vw-1));batch[0].mi.dy=(LONG)std::lround((p.y-vy)*65535.0/std::max(1,vh-1));batch[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;batch[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;batch[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
 const UINT sent=SendInput(3,batch,sizeof(INPUT));injectedPackets+=sent;if(sent!=3)throw std::runtime_error("Owned activation failed");GetPhysicalCursorPos(&injected);injectedKnown=true;pump(owner,80);clicks=mouseDrags=0;
 if(GetForegroundWindow()!=lower)throw std::runtime_error("Owned activation did not establish foreground; test stopped");
 }}
   auto s=std::make_shared<Carrier::State>();s->owner=&owner;s->id=7;s->nonce="native-input-fixture-0001";s->global=true;s->monitor=MonitorFromWindow(lower,MONITOR_DEFAULTTOPRIMARY);
   if(desktopInput){s->target=std::make_shared<Target>();s->target->window=lower;s->target->pid=GetCurrentProcessId();s->target->process=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,GetCurrentProcessId());}
   c.lpfnWndProc=Carrier::proc;c.lpszClassName=L"YMCCMonitorInputFixtureDisplay";RegisterClassW(&c);s->hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT,c.lpszClassName,L"",WS_POPUP|WS_DISABLED,left,top,150,90,nullptr,nullptr,c.hInstance,s.get());
   if(!s->hwnd||!hudinput::ensure(s->hwnd))throw std::runtime_error("Owned inert display failed");s->ready=s->armed=s->layout=true;s->width=150;s->height=90;s->seq=1;s->expires=wallNow()+60000;s->viewportKey=Carrier::viewportInfo(*s).dump();if(desktopInput){const double w=mi.rcWork.right-mi.rcWork.left,h=mi.rcWork.bottom-mi.rcWork.top;s->drag.position={(left+12-mi.rcWork.left)/w,(top+12-mi.rcWork.top)/h,true,false,1};}const auto initialPlacement=s->drag.position;owner.current=s;owner.refresh(s);pump(owner,80);
   check("actual production carrier creates bounded interactive surface",s->inputWindow&&s->input.active(),{{"inputError",s->inputError},{"panel",rectangle(s->panel)},{"dpi",GetDpiForWindow(lower)}});
   if(!s->inputWindow||!s->input.active())throw std::runtime_error("Production controlled input unavailable");
   auto recorded=s->input.source().PointerMoved([&](auto const&,auto const& a){const auto point=a.CurrentPoint();const auto p=point.Position(),raw=point.RawPosition();const auto device=point.PointerDevice();RECT r{};GetWindowRect(s->inputWindow,&r);trace.push_back({{"kind",device?(int)device.PointerDeviceType():-1},{"contact",s->drag.contact()},{"surfaceError",(long)s->input.error},{"position",{p.X,p.Y}},{"raw",{raw.X,raw.Y}},{"input",rectangle(r)},{"panel",rectangle(s->panel)}});});
   auto edge=[&](const char* phase,auto const& a){if(phase==std::string("down"))++downEvents;if(phase==std::string("up"))++upEvents;const auto p=a.CurrentPoint();const auto device=p.PointerDevice();trace.push_back({{"phase",phase},{"kind",device?(int)device.PointerDeviceType():-1},{"contact",s->drag.contact()},{"id",p.PointerId()},{"position",{p.Position().X,p.Position().Y}},{"surfaceError",(long)s->input.error},{"foreground",(UINT_PTR)GetForegroundWindow()}});};
   const auto pressToken=s->input.source().PointerPressed([&](auto const&,auto const& a){edge("down",a);});
   const auto releaseToken=s->input.source().PointerReleased([&](auto const&,auto const& a){edge("up",a);});
   const auto cancelToken=s->input.source().PointerCaptureLost([&](auto const&,auto const& a){edge("cancel",a);});
   pumpingCarrier=&owner;
   if(desktopInput){
    RECT start=s->panel;int x=start.left+40,y=start.top+35;
    const auto mouseTemplate=events.size();for(int i=0;i<2;++i){mouseAt(x,y);pump(owner);mouseAt(x,y,MOUSEEVENTF_LEFTDOWN);pump(owner);mouseAt(x,y,MOUSEEVENTF_LEFTUP);pump(owner);}
    bool mouseSwitched=false;for(size_t i=mouseTemplate;i<events.size();++i)mouseSwitched|=events[i].value("name",std::string{})=="desktopHud.template";check("mouse double click switches template without locking",mouseSwitched&&!s->drag.position.locked);
    mouseAt(x,y,MOUSEEVENTF_LEFTDOWN);pump(owner);for(int i=1;i<=8;++i){mouseAt(x+i*4,y+i*2);pump(owner);}mouseAt(x+32,y+16,MOUSEEVENTF_LEFTUP);pump(owner);
    check("unlocked mouse drags monitor one-to-one",std::abs(s->panel.left-start.left-32)<=1&&std::abs(s->panel.top-start.top-16)<=1,{{"start",rectangle(start)},{"end",rectangle(s->panel)}});
    {
     const int ax=s->panel.left+35,ay=s->panel.top+35;mouseAt(ax,ay,MOUSEEVENTF_LEFTDOWN);pump(owner);
     const bool entered=s->drag.contact();const HWND captured=GetCapture();owner.stopInteraction(s);
     const int bx=left+width-20,by=top+height-20;mouseAt(bx,by,MOUSEEVENTF_LEFTUP);pump(owner);
     const unsigned beforeClick=clicks;mouseAt(bx,by,MOUSEEVENTF_LEFTDOWN);pump(owner);mouseAt(bx,by,MOUSEEVENTF_LEFTUP);pump(owner);
     check("interrupted real mouse contact leaves lower window clickable",entered&&!s->drag.contact()&&clicks==beforeClick+1,{{"captureBefore",(UINT_PTR)captured},{"captureAfter",(UINT_PTR)GetCapture()},{"lowerClicks",clicks-beforeClick}});
     owner.refresh(s);pump(owner);
    }
    // Reset between independent input scenarios through the existing native consumer.
    s->drag.cancel();s->drag.position=initialPlacement;owner.refresh(s);pump(owner);start=s->panel;x=start.left+35;y=start.top+35;
    if(!InitializeTouchInjection(10,TOUCH_FEEDBACK_NONE))throw std::runtime_error("Touch injection unsupported: "+std::to_string(GetLastError()));
    touchAt(x,y,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(owner,20);const bool interruptedTouchEntered=s->drag.contact();owner.stopInteraction(s);touchAt(x,y,POINTER_FLAG_UP);pump(owner);
    const unsigned clicksAfterTouch=clicks;const int tx=left+width-20,ty=top+height-20;mouseAt(tx,ty,MOUSEEVENTF_LEFTDOWN);pump(owner);mouseAt(tx,ty,MOUSEEVENTF_LEFTUP);pump(owner);
    check("interrupted real touch contact leaves lower window clickable",interruptedTouchEntered&&!s->drag.contact()&&clicks==clicksAfterTouch+1,{{"captureAfter",(UINT_PTR)GetCapture()},{"lowerClicks",clicks-clicksAfterTouch}});owner.refresh(s);pump(owner);
    touchAt(x,y,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(owner,20);if(!s->drag.contact()){touchAt(x,y,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(owner,20);}check("actual touch DOWN enters production drag consumer",s->drag.contact(),{{"foreground",(UINT_PTR)GetForegroundWindow()},{"lower",(UINT_PTR)lower},{"input",(UINT_PTR)s->inputWindow},{"surfaceError",(long)s->input.error},{"sourceCapture",s->input.source().HasCapture()},{"point",{x,y}}});
    int maxError=0;Json samples=Json::array();for(int i=1;i<=12;++i){const int dx=i*4,dy=i*2;touchAt(x+dx,y+dy,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(owner);const bool pastJitter=std::hypot(dx,dy)>=7;int err=(int)std::max(std::abs((s->panel.left-start.left)-(pastJitter?dx:0)),std::abs((s->panel.top-start.top)-(pastJitter?dy:0)));maxError=std::max(maxError,(int)err);samples.push_back({{"dx",dx},{"dy",dy},{"panel",rectangle(s->panel)},{"error",err}});}
    touchAt(x+48,y+24,POINTER_FLAG_UP);pump(owner);check("small touch movements track one-to-one without amplification",maxError<=1,{{"maxError",maxError},{"samples",samples}});
    s->drag.cancel();s->drag.position=initialPlacement;owner.refresh(s);pump(owner);start=s->panel;x=start.left+35;y=start.top+35;
    touchAt(x,y,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(owner);for(int i=1;i<=10;++i){touchAt(x+i*25,y,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(owner);}touchAt(x+250,y,POINTER_FLAG_UP);pump(owner);check("touch keeps tracking beyond original input rectangle",std::abs(s->panel.left-start.left-250)<=1&&!s->drag.contact(),{{"start",rectangle(start)},{"end",rectangle(s->panel)}});
    start=s->panel;x=start.left+35;y=start.top+35;
    const auto touchTemplate=events.size();for(int i=0;i<2;++i){touchAt(x,y,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(owner,35);touchAt(x,y,POINTER_FLAG_UP);pump(owner,65);}bool touchSwitched=false;for(size_t i=touchTemplate;i<events.size();++i)touchSwitched|=events[i].value("name",std::string{})=="desktopHud.template";check("touch double tap switches template without moving or locking",touchSwitched&&!s->drag.position.locked&&s->panel.left==start.left&&s->panel.top==start.top);
    touchAt(x,y,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);for(int held=0;held<18;++held){pump(owner,40);touchAt(x,y,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);}touchAt(x,y,POINTER_FLAG_UP);pump(owner);check("touch long press with radial disabled preserves unlocked monitor",!s->drag.position.locked&&IsWindowVisible(s->inputWindow)&&!s->drag.contact());
    auto lockedPosition=s->drag.position;lockedPosition.locked=true;++lockedPosition.revision;s->drag.adopt(lockedPosition);owner.refresh(s);pump(owner);
    check("explicit settings lock hides the entire input window",s->drag.position.locked&&!IsWindowVisible(s->inputWindow)&&!s->drag.contact());
    const auto clicked=clicks,wheeled=wheels;mouseAt(x,y,MOUSEEVENTF_LEFTDOWN);pump(owner);mouseAt(x,y,MOUSEEVENTF_LEFTUP);pump(owner);mouseAt(x,y,MOUSEEVENTF_WHEEL);pump(owner);check("locked monitor passes mouse clicks and wheel to lower window",clicks==clicked+1&&wheels==wheeled+1);
    owner.refresh(s,true);pump(owner,80); // Existing foreground repair, explicit because the lower fixture shares this process.
    owner.refresh(s,true);pump(owner,80); // Same repair requested by the existing game foreground event.
    for(int i=0;i<2;++i){touchAt(x,y,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);pump(owner,35);touchAt(x,y,POINTER_FLAG_UP);pump(owner,65);}check("locked touch cannot switch or unlock the monitor",s->drag.position.locked&&!IsWindowVisible(s->inputWindow)&&s->panel.left==start.left&&s->panel.top==start.top);
    auto unlocked=s->drag.position;unlocked.locked=false;++unlocked.revision;s->drag.adopt(unlocked);owner.refresh(s);pump(owner);check("settings unlock restores the bounded input surface",!s->drag.position.locked&&IsWindowVisible(s->inputWindow)&&s->input.active());
   }else{
    RECT start=s->panel;const int x=30,y=25;auto dispatch=[&](UINT32 id,POINTER_INPUT_TYPE type,screeninput::Phase phase,float px,float py,LONGLONG age=0){screeninput::Event e{10,id,type,phase,px,py,false,(DWORD)(GetTickCount64()-age),true};Carrier::inputEvent(s->inputWindow,e,s.get());};
    dispatch(1,PT_MOUSE,screeninput::Phase::Down,(float)x,(float)y);dispatch(1,PT_MOUSE,screeninput::Phase::Move,(float)x+25,(float)y+15);dispatch(1,PT_MOUSE,screeninput::Phase::Up,(float)x+25,(float)y+15);check("production consumer accepts unlocked mouse drag",std::abs(s->panel.left-start.left-25)<=1&&std::abs(s->panel.top-start.top-15)<=1&&s->drag.position.placed,{{"before",rectangle(start)},{"after",rectangle(s->panel)}});
    s->drag.cancel();s->drag.position=initialPlacement;owner.refresh(s);start=s->panel;RECT inputStart{};GetWindowRect(s->inputWindow,&inputStart);int maxError=0;
    dispatch(2,PT_TOUCH,screeninput::Phase::Down,(float)x,(float)y);for(int i=1;i<=12;++i){dispatch(2,PT_TOUCH,screeninput::Phase::Move,(float)x+i*4,(float)y+i*2);const bool pastJitter=std::hypot(i*4,i*2)>=7;maxError=std::max(maxError,(int)std::max(std::abs(s->panel.left-start.left-(pastJitter?i*4:0)),std::abs(s->panel.top-start.top-(pastJitter?i*2:0))));}
    RECT during{};GetWindowRect(s->inputWindow,&during);check("touch coordinates cannot feed back through moving input HWND",maxError<=1&&during.left==inputStart.left&&during.top==inputStart.top,{{"maxError",maxError},{"inputStart",rectangle(inputStart)},{"inputDuring",rectangle(during)}});dispatch(2,PT_TOUCH,screeninput::Phase::Up,(float)x+48,(float)y+24);RECT after{};GetWindowRect(s->inputWindow,&after);check("input follows final panel after release",after.left==s->panel.left&&after.top==s->panel.top&&!s->drag.contact());
    // Replay the live log's future/foreign device stamp at the real carrier.
    dispatch(21,PT_TOUCH,screeninput::Phase::Down,30,25);dispatch(21,PT_TOUCH,screeninput::Phase::Move,65,45);dispatch(21,PT_TOUCH,screeninput::Phase::Up,65,45,-1000);
    check("future input timestamp cannot poison saved drag revision",!s->drag.contact()&&s->drag.position.revision<=9007199254740991ULL,{{"revision",s->drag.position.revision}});
    bool safeEnd=false;for(const auto& event:events)if(event.value("name",std::string{})=="desktopHud.placement"){const auto& value=event["value"];safeEnd=!value.value("dragging",true)&&value["placement"].value("dragRevision",0ULL)<=9007199254740991ULL;}
    check("native drag end publishes a safe idle placement receipt",safeEnd);
    for(const auto type:{PT_MOUSE,PT_TOUCH}){const auto before=events.size();for(int i=0;i<2;++i){dispatch(3,type,screeninput::Phase::Down,30,25);dispatch(3,type,screeninput::Phase::Up,30,25);}bool switched=false;for(size_t i=before;i<events.size();++i)switched|=events[i].value("name",std::string{})=="desktopHud.template";check("native consumer double click/tap emits template switch without locking",switched&&!s->drag.position.locked);s->drag.cancel();auto reset=s->drag.position;reset.locked=false;++reset.revision;s->drag.adopt(reset);owner.refresh(s);}
    dispatch(4,PT_TOUCH,screeninput::Phase::Down,30,25);pump(owner,680);check("radial disabled long hold preserves monitor drag availability",!s->drag.position.locked&&IsWindowVisible(s->inputWindow)&&s->drag.contact());dispatch(4,PT_TOUCH,screeninput::Phase::Up,30,25);s->drag.position.locked=true;++s->drag.position.revision;owner.refresh(s);
    const auto lockedPanel=s->panel;dispatch(5,PT_MOUSE,screeninput::Phase::Down,30,25);dispatch(5,PT_MOUSE,screeninput::Phase::Move,80,55);dispatch(5,PT_MOUSE,screeninput::Phase::Up,80,55);check("locked mouse cannot move the monitor",s->panel.left==lockedPanel.left&&s->panel.top==lockedPanel.top&&!s->drag.contact());
    auto unlocked=s->drag.position;unlocked.locked=false;++unlocked.revision;s->drag.adopt(unlocked);owner.refresh(s);check("settings unlock restores the actual bounded input window",!s->drag.position.locked&&IsWindowVisible(s->inputWindow)&&s->input.active());
    // The actual native timer may arm a hold, but only its matching release
    // can call the production summon callback. No physical input is injected.
    monitorHoldEnabled=true;
    auto resetHold=[&](bool locked=false){owner.stopInteraction(s);s->drag.position.locked=locked;++s->drag.position.revision;owner.refresh(s);};
    auto holdTimer=[&]{SendMessageW(s->hwnd,WM_TIMER,Carrier::holdTimer,0);};
    resetHold();unsigned beforeSummons=radialSummons;
    dispatch(30,PT_TOUCH,screeninput::Phase::Down,30,25,700);holdTimer();
    check("monitor hold threshold only arms until release",radialSummons==beforeSummons&&s->radialContact&&IsWindowVisible(s->hwnd)&&s->drag.contact());
    holdTimer();check("repeated monitor hold timer cannot summon",radialSummons==beforeSummons);
    dispatch(30,PT_TOUCH,screeninput::Phase::Up,30,25);check("matching release summons once",radialSummons==beforeSummons+1&&!s->radialContact&&!s->drag.contact());
    dispatch(30,PT_TOUCH,screeninput::Phase::Up,30,25);holdTimer();check("late release and timer cannot repeat summon",radialSummons==beforeSummons+1);
    resetHold();beforeSummons=radialSummons;dispatch(31,PT_TOUCH,screeninput::Phase::Down,30,25,700);holdTimer();dispatch(31,PT_TOUCH,screeninput::Phase::Cancel,30,25);dispatch(31,PT_TOUCH,screeninput::Phase::Up,30,25);
    check("cancel after threshold revokes summon",radialSummons==beforeSummons&&!s->radialContact&&!s->drag.contact());
    resetHold();beforeSummons=radialSummons;dispatch(32,PT_TOUCH,screeninput::Phase::Down,30,25,700);holdTimer();dispatch(32,PT_TOUCH,screeninput::Phase::Move,65,45);dispatch(32,PT_TOUCH,screeninput::Phase::Up,65,45);
    check("movement after threshold still drags without summoning",radialSummons==beforeSummons&&!s->radialContact&&!s->drag.contact());
    resetHold();beforeSummons=radialSummons;dispatch(33,PT_TOUCH,screeninput::Phase::Down,30,25,700);holdTimer();dispatch(34,PT_TOUCH,screeninput::Phase::Down,30,25);dispatch(34,PT_TOUCH,screeninput::Phase::Up,30,25);
    check("second contact cannot release an armed monitor hold",radialSummons==beforeSummons&&s->radialContact&&s->radialContactId==33);
    dispatch(33,PT_TOUCH,screeninput::Phase::Up,30,25);check("original contact retains release ownership",radialSummons==beforeSummons+1);
    resetHold();beforeSummons=radialSummons;dispatch(35,PT_TOUCH,screeninput::Phase::Down,30,25,700);holdTimer();owner.stopInteraction(s);owner.refresh(s);dispatch(35,PT_TOUCH,screeninput::Phase::Up,30,25);
    check("hide cancellation revokes an armed contact",radialSummons==beforeSummons&&!s->radialContact);
    resetHold(true);beforeSummons=radialSummons;dispatch(36,PT_TOUCH,screeninput::Phase::Down,30,25,700);holdTimer();check("locked monitor hold also waits for release",radialSummons==beforeSummons&&s->radialContact&&s->drag.position.locked);dispatch(36,PT_TOUCH,screeninput::Phase::Up,30,25);check("locked monitor matching release summons",radialSummons==beforeSummons+1&&s->drag.position.locked);
    resetHold();beforeSummons=radialSummons;dispatch(37,PT_TOUCH,screeninput::Phase::Down,30,25,700);dispatch(37,PT_TOUCH,screeninput::Phase::Up,30,25);check("delayed down and up summon without needing timer delivery",radialSummons==beforeSummons+1);
    resetHold();beforeSummons=radialSummons;dispatch(38,PT_TOUCH,screeninput::Phase::Down,30,25);dispatch(38,PT_TOUCH,screeninput::Phase::Up,30,25);check("short monitor tap never summons or locks",radialSummons==beforeSummons&&!s->drag.position.locked);
    resetHold();radialHoldDuration=1000;beforeSummons=radialSummons;dispatch(39,PT_TOUCH,screeninput::Phase::Down,30,25,700);dispatch(39,PT_TOUCH,screeninput::Phase::Up,30,25);check("custom hold duration cannot fall through to drag lock",radialSummons==beforeSummons&&!s->drag.position.locked);
    resetHold();radialHoldDuration=400;beforeSummons=radialSummons;dispatch(40,PT_TOUCH,screeninput::Phase::Down,30,25,450);holdTimer();check("custom short hold still waits for release",radialSummons==beforeSummons&&s->radialContact);dispatch(40,PT_TOUCH,screeninput::Phase::Up,30,25);check("custom short hold releases once",radialSummons==beforeSummons+1);
    resetHold();radialHoldDuration=650;beforeSummons=radialSummons;dispatch(41,PT_PEN,screeninput::Phase::Down,30,25,700);holdTimer();check("pen monitor hold waits for release",radialSummons==beforeSummons);dispatch(41,PT_PEN,screeninput::Phase::Up,30,25);check("pen release uses the same monitor flow",radialSummons==beforeSummons+1);
    resetHold();monitorHoldEnabled=false;
   }
   if(!desktopInput){
    auto mainRecorder=Microsoft::WRL::Make<NoticeControllerRecorder>();s->controller=mainRecorder;
    owner.refresh(s);s->armed=false;owner.refresh(s);
    check("main HUD hide suspends renderer and its entire input surface",!mainRecorder->visible&&!IsWindowVisible(s->hwnd)&&!IsWindowVisible(s->inputWindow)&&!s->input.hasContacts());
    // A pending layout may measure while the display stays hidden; this is
    // distinct from idle/disabled/expired output and owns no input surface.
    s->armed=true;s->layout=false;owner.refresh(s);
    check("pending main layout resumes measurement without interactive overlay",mainRecorder->visible&&!IsWindowVisible(s->hwnd)&&!IsWindowVisible(s->inputWindow));
    s->layout=true;owner.refresh(s);
    check("main HUD layout restores renderer and bounded input together",mainRecorder->visible&&IsWindowVisible(s->hwnd)&&IsWindowVisible(s->inputWindow));
    s->visibility=hudvisibility::Mode::GameRunning;s->policyGamePid=0;owner.refresh(s);
    check("game visibility gate releases main HUD renderer and input",!mainRecorder->visible&&!IsWindowVisible(s->hwnd)&&!IsWindowVisible(s->inputWindow));
    s->visibility=hudvisibility::Mode::Always;owner.refresh(s);
    s->expires=wallNow()-1;owner.refresh(s);
    check("expired main transport releases renderer and input without another frame",!mainRecorder->visible&&!IsWindowVisible(s->hwnd)&&!IsWindowVisible(s->inputWindow));
    s->expires=wallNow()+60000;owner.refresh(s);
    check("fresh main transport can restore the existing owned surface",mainRecorder->visible&&IsWindowVisible(s->hwnd)&&IsWindowVisible(s->inputWindow));
   }
   if(!desktopInput){
    auto source=s->input.source();source.SetPointerCapture();
    check("controlled monitor source can acquire its own capture",source.HasCapture());
    owner.stopInteraction(s);
    check("ordinary monitor hide releases independent pointer capture",!source.HasCapture(),{{"sourceHasCapture",source.HasCapture()},{"win32Capture",(UINT_PTR)GetCapture()}});
    Carrier::inputEvent(s->inputWindow,screeninput::Event{10,701,PT_MOUSE,screeninput::Phase::Down,30,25,false},s.get());
    check("late down cannot restart a hidden monitor input owner",!s->drag.contact()&&!s->radialContact);
    owner.refresh(s);
    source.SetPointerCapture();ShowWindow(s->inputWindow,SW_HIDE);
    check("externally hidden monitor input releases independent capture",!source.HasCapture()&&!s->drag.contact());
    owner.refresh(s);source.SetPointerCapture();
    Carrier::inputEvent(s->inputWindow,screeninput::Event{10,700,PT_MOUSE,screeninput::Phase::Down,30,25,false},s.get());
    SendMessageW(s->inputWindow,WM_CANCELMODE,0,0);
    check("shell cancel mode withdraws monitor contact and capture",!source.HasCapture()&&!s->drag.contact()&&!IsWindowVisible(s->inputWindow));
    owner.refresh(s);
    SetCapture(s->inputWindow);check("fixture owns monitor Win32 capture",GetCapture()==s->inputWindow);
    owner.stopInteraction(s);
    check("monitor hide releases only its own Win32 capture",GetCapture()!=s->inputWindow);
    owner.refresh(s);
    HWND otherRoot=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,c.lpszClassName,L"Other controlled input owner",WS_POPUP,left+300,top+200,100,70,nullptr,nullptr,c.hInstance,nullptr);
    screeninput::Surface other;bool otherAttached=otherRoot&&other.attach(runtime,otherRoot,11,nullptr,nullptr,true,true);
    if(!otherAttached)throw std::runtime_error("Other capture owner fixture unavailable");
    ShowWindow(otherRoot,SW_SHOWNOACTIVATE);other.source().SetPointerCapture();const HWND otherCaptured=GetCapture();
    owner.stopInteraction(s);
    check("monitor cancellation preserves another controlled source capture",other.source().HasCapture()&&GetCapture()==otherCaptured);
    HWND closingRoot=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,c.lpszClassName,L"Retiring controlled input owner",WS_POPUP,left+400,top+200,100,70,nullptr,nullptr,c.hInstance,nullptr);
    screeninput::Surface closingSurface;if(!closingRoot||!closingSurface.attach(runtime,closingRoot,12,nullptr,nullptr,true,true))throw std::runtime_error("Closing capture owner fixture unavailable");
    closingSurface.quiesce();closingSurface.close();DestroyWindow(closingRoot);
    check("controlled surface destruction preserves another source capture",other.source().HasCapture()&&GetCapture()==otherCaptured);
    other.quiesce();other.close();DestroyWindow(otherRoot);owner.refresh(s);
    const bool previousCrosshair=s->crosshair;s->crosshair=true;owner.refresh(s);
    RECT inputRect{},displayRect{};GetWindowRect(s->inputWindow,&inputRect);GetWindowRect(s->hwnd,&displayRect);
    check("fullscreen mouse status never expands the draggable input region",inputRect.right-inputRect.left==s->width&&inputRect.bottom-inputRect.top==s->height&&displayRect.right-displayRect.left==mi.rcMonitor.right-mi.rcMonitor.left);
    POINT outside{left+width-15,top+height-15};
    check("fullscreen monitor display leaves the lower native window hittable outside its panel",GetAncestor(WindowFromPoint(outside),GA_ROOT)==lower);
    source.SetPointerCapture();s->visibility=hudvisibility::Mode::GameRunning;s->policyGamePid=0;owner.refresh(s);
    check("game gate withdraws independent capture before a later show",!source.HasCapture());
    s->visibility=hudvisibility::Mode::Always;owner.refresh(s);source.SetPointerCapture();s->expires=wallNow()-1;owner.refresh(s);
    check("transport expiry withdraws independent capture",!source.HasCapture());
    s->expires=wallNow()+60000;s->crosshair=previousCrosshair;owner.refresh(s);
   }
   // Exercise the same Carrier used for monitor notices, with only the COM
   // renderer terminal recorded. Main controlled input stays active throughout.
   if(!desktopInput){
    Carrier noticeOwner;unsigned noticeHaptics=0;noticeOwner.initialize(nullptr,{nullptr,nullptr,nullptr,true,&runtime,[&](bool,bool){++noticeHaptics;}});
    const auto mainInput=s->inputWindow;const auto originalCapture=GetCapture();auto recorder=Microsoft::WRL::Make<NoticeControllerRecorder>();
    auto n=std::make_shared<Carrier::State>();n->owner=&noticeOwner;n->id=11;n->nonce="notice-input-lifecycle-0001";n->global=true;n->notice=true;n->monitor=s->monitor;n->ready=true;n->width=120;n->height=80;n->viewportKey=Carrier::viewportInfo(*n).dump();n->controller=recorder;
    n->hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT,c.lpszClassName,L"",WS_POPUP|WS_DISABLED,left,top,120,80,nullptr,nullptr,c.hInstance,n.get());
    if(!n->hwnd||!hudinput::ensure(n->hwnd))throw std::runtime_error("Notice fixture HWND failed");noticeOwner.current=n;
    noticeOwner.refresh(n);check("hidden pre-owned notice suspends its renderer",!recorder->visible&&!IsWindowVisible(n->hwnd));
    auto noticeFrame=[&](unsigned long long seq){const auto expires=wallNow()+5000;return Json{{"type","monitor-notice"},{"protocol",1},{"hudId",n->id},{"nonce",n->nonce},{"seq",seq},{"expiresAt",expires},{"feedback",{{"expiresAtUnixMs",expires}}}};};
    for(unsigned long long seq=1;seq<=3;++seq){
     check("admitted notice frame resumes only for its new layout",noticeOwner.noticeFrame(n->id,n->nonce,noticeFrame(seq))&&recorder->visible&&!n->layout&&!IsWindowVisible(n->hwnd));
     n->layout=true;noticeOwner.refresh(n);check("notice display and renderer become visible together",IsWindowVisible(n->hwnd)&&recorder->visible);
     check("notice never creates a controlled input surface",!n->inputWindow&&!n->input.active()&&noticeHaptics==0);
     check("old notice hide cannot suspend a newer display",!noticeOwner.noticeHide(n->id,n->nonce,seq-1)&&recorder->visible);
     if(seq==1)noticeOwner.noticeHide(n->id,n->nonce,seq);
     else if(seq==2)SendMessageW(n->hwnd,WM_TIMER,Carrier::leaseTimer,0);
     else ShowWindow(n->hwnd,SW_HIDE);
     check("explicit hide expiry and external hide suspend notice renderer",!IsWindowVisible(n->hwnd)&&!recorder->visible);
     noticeOwner.refresh(n);check("hidden notice cannot retain desktop capture or another input surface",GetCapture()==originalCapture&&s->inputWindow==mainInput&&s->input.active()&&!n->inputWindow);
    }
    noticeOwner.noticeHide(n->id,n->nonce,3);const auto noticeHwnd=n->hwnd;
    check("notice close hides controller before destroying its window",noticeOwner.close(n->id,n->nonce)&&recorder->closed&&!recorder->visibleAtClose&&!IsWindow(noticeHwnd));
    check("notice destruction preserves the existing monitor input owner",s->inputWindow==mainInput&&s->input.active()&&GetCapture()==originalCapture&&noticeHaptics==0);
   }
   if(!desktopInput){
    // Real Win32 messages to our private HWNDs exercise the same route used by
    // message-based remote mouse clients. No desktop input is synthesized.
    const auto initial=s->drag.position;
    auto resetMouse=[&]{owner.stopInteraction(s);s->drag.position=initial;s->drag.position.locked=false;monitorHoldEnabled=false;owner.refresh(s);};
    auto mouseMessage=[&](HWND window,UINT message,WPARAM buttons,int x,int y){return SendMessageW(window,message,buttons,MAKELPARAM(x,y));};
    resetMouse();const auto start=s->panel;const HWND input=s->inputWindow;
    check("monitor keeps mouse out of the independent touch controller",ordinaryMouseRoute(s->input));
    POINT inside{start.left+30,start.top+25};check("ordinary HWND mouse target is confined to the monitor panel",GetAncestor(WindowFromPoint(inside),GA_ROOT)==input);
    mouseMessage(input,WM_LBUTTONDOWN,MK_LBUTTON,30,25);
    check("native mouse down captures only its bounded monitor window",s->drag.contact()&&GetCapture()==input);
    mouseMessage(input,WM_MOUSEMOVE,MK_LBUTTON,62,41);
    check("message-based remote mouse drags through the production consumer",s->drag.dragging()&&std::abs(s->panel.left-start.left-32)<=1&&std::abs(s->panel.top-start.top-16)<=1);
    mouseMessage(input,WM_LBUTTONUP,0,62,41);
    check("native mouse up releases capture without hiding the monitor",!s->drag.contact()&&GetCapture()!=input&&IsWindowVisible(s->inputWindow));
    resetMouse();mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,30,25);const bool missingUpStarted=s->drag.contact();mouseMessage(s->inputWindow,WM_MOUSEMOVE,0,32,27);
    check("missing mouse up is cancelled on the next released-button move",missingUpStarted&&!s->drag.contact()&&GetCapture()!=s->inputWindow);
    resetMouse();mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,30,25);const bool transferStarted=s->drag.contact();SetCapture(lower);
    check("capture transfer cancels monitor gesture without releasing the new owner",transferStarted&&!s->drag.contact()&&GetCapture()==lower);ReleaseCapture();
    resetMouse();mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,30,25);const bool cancelStarted=s->drag.contact();SendMessageW(s->inputWindow,WM_CANCELMODE,0,0);
    check("native mouse cancel withdraws capture and hidden input",cancelStarted&&!s->drag.contact()&&GetCapture()!=s->inputWindow&&!IsWindowVisible(s->inputWindow));
    resetMouse();mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,s->width+10,25);
    check("out-of-panel remote mouse down cannot acquire monitor capture",!s->drag.contact()&&GetCapture()!=s->inputWindow);
    resetMouse();auto child=s->input.child();const auto childStart=s->panel;
    mouseMessage(child,WM_LBUTTONDOWN,MK_LBUTTON,30,25);mouseMessage(child,WM_MOUSEMOVE,MK_LBUTTON,55,40);mouseMessage(child,WM_LBUTTONUP,0,55,40);
    check("mouse messages addressed to the owned child retain bounded dragging",std::abs(s->panel.left-childStart.left-25)<=1&&std::abs(s->panel.top-childStart.top-15)<=1&&!s->drag.contact());
    resetMouse();const auto templateEvents=events.size();
    for(int i=0;i<2;++i){mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,30,25);mouseMessage(s->inputWindow,WM_LBUTTONUP,0,30,25);}
    bool switched=false;for(size_t i=templateEvents;i<events.size();++i)switched|=events[i].value("name",std::string{})=="desktopHud.template";
    check("message-based mouse double click still switches template without locking",switched&&!s->drag.position.locked&&!s->drag.contact());
    resetMouse();const auto previousExtra=SetMessageExtraInfo(0xFF515780);mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,30,25);SetMessageExtraInfo(previousExtra);
    check("promoted touch mouse messages cannot duplicate the controlled touch gesture",!s->drag.contact()&&GetCapture()!=s->inputWindow);
    resetMouse();bool outsidePass=true;const auto panel=s->panel;
    for(POINT local:{POINT{2,2},POINT{width-3,2},POINT{2,height-3},POINT{width-3,height-3}}){POINT point{left+local.x,top+local.y};if(!PtInRect(&panel,point))outsidePass&=GetAncestor(WindowFromPoint(point),GA_ROOT)==lower;}
    check("idle draggable monitor leaves the rest of the desktop target hittable",outsidePass&&GetCapture()!=s->inputWindow);
    auto locked=s->drag.position;locked.locked=true;++locked.revision;s->drag.adopt(locked);owner.refresh(s);
    mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,30,25);
    check("explicit input lock rejects even directly addressed remote mouse down",!s->drag.contact()&&GetCapture()!=s->inputWindow&&!IsWindowVisible(s->inputWindow));
    resetMouse();mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,30,25);const bool repeatedDownStarted=s->drag.contact();
    mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,s->width+20,25);
    check("a new outside press retires the prior remote press after a lost up",repeatedDownStarted&&!s->drag.contact()&&GetCapture()!=s->inputWindow);
    resetMouse();bool repeatClean=true;for(unsigned i=0;i<5;++i){
     mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,30,25);repeatClean&=s->drag.contact()&&GetCapture()==s->inputWindow;
     owner.stopInteraction(s);mouseMessage(s->inputWindow,WM_LBUTTONUP,0,30,25);owner.refresh(s);
     repeatClean&=!s->drag.contact()&&GetCapture()!=s->inputWindow&&IsWindowVisible(s->inputWindow);
    }
    check("five remote mouse hide and reshow cycles leave no held capture",repeatClean);
    resetMouse();const HWND foregroundBefore=GetForegroundWindow();
    mouseMessage(s->inputWindow,WM_LBUTTONDOWN,MK_LBUTTON,30,25);mouseMessage(s->inputWindow,WM_LBUTTONUP,0,30,25);
    check("ordinary mouse gesture cannot activate or focus the monitor window",GetForegroundWindow()==foregroundBefore&&GetFocus()!=s->inputWindow);
    HWND siblingRoot=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE,L"YMCCMonitorInputFixtureLower",L"Other input while monitor idle",WS_POPUP,left+400,top+150,90,60,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    screeninput::Surface sibling;if(!siblingRoot||!sibling.attach(runtime,siblingRoot,13,nullptr,nullptr,true,true))throw std::runtime_error("Sibling mouse fixture unavailable");
    ShowWindow(siblingRoot,SW_SHOWNOACTIVATE);sibling.source().SetPointerCapture();const HWND siblingCapture=GetCapture();
    mouseMessage(s->inputWindow,WM_MOUSEMOVE,0,30,25);mouseMessage(s->inputWindow,WM_LBUTTONUP,0,30,25);
    check("idle monitor mouse messages preserve another controlled source capture",sibling.source().HasCapture()&&GetCapture()==siblingCapture);
    sibling.quiesce();sibling.close();DestroyWindow(siblingRoot);resetMouse();
    // Execute the production WM_POINTER mouse branch with only OS frame
    // metadata supplied by an inert terminal. Actual HWND/capture/consumer stay real.
    auto pointerMessage=[&](HWND target,UINT message,POINTER_FLAGS flags,POINTER_BUTTON_CHANGE_TYPE change,int x,int y,POINTER_INPUT_TYPE type=PT_MOUSE){
     POINT point{x,y};ClientToScreen(s->inputWindow,&point);pointerMetadata={};pointerMetadata.pointerType=type;pointerMetadata.pointerId=4001;pointerMetadata.pointerFlags=flags;pointerMetadata.ButtonChangeType=change;pointerMetadata.ptPixelLocation=point;
     pointerMetadataEnabled=true;SendMessageW(target,message,4001,MAKELPARAM(point.x,point.y));pointerMetadataEnabled=false;
    };
    const auto pointerStart=s->panel;
    pointerMessage(s->inputWindow,WM_POINTERDOWN,POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN,30,25);
    check("system mouse Pointer down enters the same bounded HWND route",s->drag.contact()&&GetCapture()==s->inputWindow);
    pointerMessage(s->inputWindow,WM_POINTERUPDATE,POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_NONE,58,39);
    pointerMessage(s->inputWindow,WM_POINTERUP,POINTER_FLAG_NONE,POINTER_CHANGE_FIRSTBUTTON_UP,58,39);
    check("system mouse Pointer drag releases capture through the same consumer",std::abs(s->panel.left-pointerStart.left-28)<=1&&std::abs(s->panel.top-pointerStart.top-14)<=1&&!s->drag.contact()&&GetCapture()!=s->inputWindow);
    resetMouse();const auto pointerChildStart=s->panel;
    pointerMessage(s->input.child(),WM_POINTERDOWN,POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN,30,25);
    pointerMessage(s->input.child(),WM_POINTERUPDATE,POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_NONE,52,37);
    pointerMessage(s->input.child(),WM_POINTERUP,POINTER_FLAG_NONE,POINTER_CHANGE_FIRSTBUTTON_UP,52,37);
    check("child-targeted system mouse Pointer messages retain bounded dragging",std::abs(s->panel.left-pointerChildStart.left-22)<=1&&std::abs(s->panel.top-pointerChildStart.top-12)<=1&&!s->drag.contact());
    resetMouse();pointerMessage(s->inputWindow,WM_POINTERDOWN,POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN,30,25,PT_TOUCH);
    check("native Pointer branch never duplicates independent Touch contacts",!s->drag.contact()&&GetCapture()!=s->inputWindow);
    pointerMessage(s->inputWindow,WM_POINTERDOWN,POINTER_FLAG_SECONDBUTTON,POINTER_CHANGE_SECONDBUTTON_DOWN,30,25);
    check("secondary mouse Pointer button cannot acquire drag capture",!s->drag.contact()&&GetCapture()!=s->inputWindow);
    resetMouse();pointerMessage(s->inputWindow,WM_POINTERDOWN,POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN,30,25);
    const bool pointerLossStarted=s->drag.contact();SendMessageW(s->inputWindow,WM_POINTERCAPTURECHANGED,4001,reinterpret_cast<LPARAM>(lower));
    check("mouse Pointer capture loss retires its gesture and owned Win32 capture",pointerLossStarted&&!s->drag.contact()&&GetCapture()!=s->inputWindow);
    resetMouse();pointerMessage(s->inputWindow,WM_POINTERDOWN,POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN,30,25);
    SendMessageW(s->inputWindow,WM_POINTERCAPTURECHANGED,4002,reinterpret_cast<LPARAM>(lower));
    check("another pointer cannot cancel the monitor mouse capture owner",s->drag.contact()&&GetCapture()==s->inputWindow);
    pointerMessage(s->inputWindow,WM_POINTERUP,POINTER_FLAG_NONE,POINTER_CHANGE_FIRSTBUTTON_UP,30,25);resetMouse();
    pointerMessage(s->inputWindow,WM_POINTERDOWN,POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN,30,25);const bool cancelPointerStarted=s->drag.contact();
    pointerMessage(s->inputWindow,WM_POINTERUPDATE,POINTER_FLAG_CANCELED|POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_NONE,30,25);
    check("cancelled system mouse Pointer frame cannot retain capture or a template tap",cancelPointerStarted&&!s->drag.contact()&&GetCapture()!=s->inputWindow);resetMouse();
   }
   // Repeated native HWND loss must retire the corresponding owned input.
   // These are real windows and controlled sources on the private desktop;
   // only the WebView controller boundary is recorded.
   if(!desktopInput){for(unsigned round=0;round<3;++round){for(bool displayLost:{false,true}){
    Carrier damaged;damaged.initialize(nullptr,{nullptr,nullptr,nullptr,false,&runtime});
    auto record=Microsoft::WRL::Make<NoticeControllerRecorder>();auto d=std::make_shared<Carrier::State>();
    d->owner=&damaged;d->id=100+round*2+displayLost;d->nonce="window-loss-fixture-0001";d->global=true;d->monitor=s->monitor;
    d->ready=d->armed=d->layout=true;d->width=150;d->height=90;d->seq=1;d->expires=wallNow()+60000;d->controller=record;
    d->viewportKey=Carrier::viewportInfo(*d).dump();
    d->hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT,c.lpszClassName,L"",WS_POPUP|WS_DISABLED,left,top,150,90,nullptr,nullptr,c.hInstance,d.get());
    if(!d->hwnd||!hudinput::ensure(d->hwnd))throw std::runtime_error("Window-loss fixture display unavailable");
    damaged.current=d;damaged.refresh(d);if(!d->inputWindow||!d->input.active())throw std::runtime_error("Window-loss fixture input unavailable");
    auto captured=d->input.source();captured.SetPointerCapture();const HWND input=d->inputWindow,display=d->hwnd;
    Carrier::inputEvent(input,screeninput::Event{10,900+round,PT_MOUSE,screeninput::Phase::Down,30,25,false},d.get());
    if(displayLost){
     DestroyWindow(display);
     check("external display destruction retires its input and controller",!damaged.current&&!d->input.active()&&!IsWindow(input)&&!captured.HasCapture()&&record->closed,{{"round",round},{"inputStillVisible",IsWindowVisible(input)!=FALSE},{"capture",captured.HasCapture()}});
     check("late refresh cannot recreate an externally destroyed display owner",(damaged.refresh(d),!IsWindow(input)&&!damaged.current));
    }else{
     DestroyWindow(input);
     check("external input destruction withdraws capture and stale HWND",!d->inputWindow&&!d->input.active()&&!d->drag.contact()&&!captured.HasCapture(),{{"round",round}});
     damaged.refresh(d);
     check("next admitted refresh rebuilds the lost bounded input surface",d->inputWindow&&IsWindow(d->inputWindow)&&IsWindowVisible(d->inputWindow)&&d->input.active()&&!d->drag.contact());
    }
    damaged.close(d->id,d->nonce);damaged.close(d->id,d->nonce);
    check("repeated damaged-owner cleanup preserves the other monitor",s->inputWindow&&IsWindow(s->inputWindow)&&s->input.active());
   }}}
   pumpingCarrier=nullptr;s->input.source().PointerPressed(pressToken);s->input.source().PointerReleased(releaseToken);s->input.source().PointerCaptureLost(cancelToken);s->input.source().PointerMoved(recorded);owner.stopInteraction(s,true);check("shutdown revokes interactive surface",!s->inputWindow&&!s->drag.contact());owner.close(s->id,s->nonce);DestroyWindow(lower);lower=nullptr;
  }
  winrt::uninit_apartment();
 }catch(const winrt::hresult_error& e){ok=false;error="WinRT:"+std::to_string((long)e.code());}catch(const std::exception& e){ok=false;error=e.what();}
 pumpingCarrier=nullptr;
 if(touchHeld){POINTER_TOUCH_INFO a{};a.pointerInfo.pointerType=PT_TOUCH;a.pointerInfo.pointerId=1;a.pointerInfo.ptPixelLocation=touchInjected;a.pointerInfo.pointerFlags=POINTER_FLAG_UP;InjectTouchInput(1,&a);}
 if(mouseHeld){INPUT up{};up.type=INPUT_MOUSE;up.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&up,sizeof up);}
 HWND focused=GetForegroundWindow();if(desktopInput&&injectedKnown){POINT current{};GetPhysicalCursorPos(&current);restoreCursor=(current.x==injected.x&&current.y==injected.y)||(lastWasTouch&&current.x==touchInjected.x&&current.y==touchInjected.y);if(restoreCursor)SetCursorPos(originalCursor.x,originalCursor.y);}
 if(lower){DestroyWindow(lower);lower=nullptr;}if(desktopInput&&IsWindow(originalForeground)&&(focused==nullptr||GetWindowThreadProcessId(focused,nullptr)==GetCurrentThreadId()))SetForegroundWindow(originalForeground);
 if(privateDesk){SetThreadDesktop(originalDesktop);CloseDesktop(privateDesk);}
 const Json result={{"ok",ok},{"cases",cases},{"error",error},{"events",events},{"trace",trace},{"lowerPointerEvents",lowerPointers},{"systemInputInjected",injectedPackets>0},{"injectedPackets",injectedPackets},{"privateDesktop",!desktopInput},{"visibleDesktopSwitched",false},{"mouseRestored",restoreCursor},{"physicalHaptics",false}};std::ofstream(path)<<result.dump(2);std::cout<<Json{{"ok",ok},{"cases",cases.size()},{"error",error}}.dump()<<std::endl;return ok?0:3;
}
