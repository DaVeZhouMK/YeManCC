#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static bool pointerMetadataEnabled=false;
static UINT32 failedPointerMetadataId=0;
static HWND failedClientConversion=nullptr;
static BOOL fixtureScreenToClient(HWND window,POINT* point){if(window==failedClientConversion){SetLastError(ERROR_INVALID_WINDOW_HANDLE);return FALSE;}return ScreenToClient(window,point);}
static POINTER_INFO pointerMetadata{};
static BOOL fixtureGetPointerInfo(UINT32 id,POINTER_INFO* value){
 if(id==failedPointerMetadataId){SetLastError(ERROR_NOT_FOUND);return FALSE;}
 if(pointerMetadataEnabled&&id==pointerMetadata.pointerId){*value=pointerMetadata;return TRUE;}
 return GetPointerInfo(id,value);
}
#define GetPointerInfo fixtureGetPointerInfo
#define ScreenToClient fixtureScreenToClient
#include "monitor_desktop_hud.test.h"
#undef GetPointerInfo
#undef ScreenToClient
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
// Message replay runs on a private, never-displayed desktop. The real OS route
// and second-process receiver are separately exercised by monitor-routing.
static thread_local bool failNextAllocation=false;
void* operator new(std::size_t size){if(failNextAllocation){failNextAllocation=false;throw std::bad_alloc();}if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept{std::free(p);}void operator delete(void* p,std::size_t) noexcept{std::free(p);}
template<class State> static Json inputWithdrawalState(const State& s){if constexpr(requires{s.inputWithdrawal;})return s.inputWithdrawal;else return nullptr;}
static WNDPROC originalPlacementProc=nullptr;static bool inflateNextPlacement=false;
static LRESULT CALLBACK placementProc(HWND h,UINT message,WPARAM w,LPARAM l){const auto result=CallWindowProcW(originalPlacementProc,h,message,w,l);if(message==WM_WINDOWPOSCHANGING&&inflateNextPlacement){auto* pos=reinterpret_cast<WINDOWPOS*>(l);if(!(pos->flags&SWP_NOSIZE)){pos->cx+=300;inflateNextPlacement=false;}}return result;}
static LRESULT CALLBACK lowerProc(HWND h,UINT m,WPARAM w,LPARAM l){return DefWindowProcW(h,m,w,l);}
static void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
static Json rectangle(const RECT& r){return {{"left",r.left},{"top",r.top},{"width",r.right-r.left},{"height",r.bottom-r.top}};}
int wmain(int argc,wchar_t** argv){
 if(argc!=3)return 2;Json cases=Json::array(),events=Json::array();bool ok=true;std::string error;
 const auto check=[&](const char* name,bool good,Json detail=Json::object()){ok&=good;cases.push_back({{"name",name},{"ok",good},{"detail",detail}});};
 const HDESK original=GetThreadDesktop(GetCurrentThreadId());HDESK desk=CreateDesktopW((L"YMCC-Bounded-Input-"+std::to_wstring(GetCurrentProcessId())).c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
 HWND lower=nullptr;bool com=false;
 try{
  need(desk&&SetThreadDesktop(desk),"Private input desktop unavailable");SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  winrt::init_apartment(winrt::apartment_type::single_threaded);com=true;
  {
   screeninput::Runtime runtime;Carrier owner;unsigned summons=0;bool radial=false;
   Config cfg;cfg.inputRuntime=&runtime;cfg.notify=[&](const std::string& name,const Json& value){events.push_back({{"name",name},{"value",value}});};
   cfg.radialHoldEnabled=[&]{return radial;};cfg.radialHoldDuration=[] {return 650u;};cfg.radialSummon=[&](int,int){++summons;};owner.initialize(nullptr,cfg);
   WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=lowerProc;cls.lpszClassName=L"YMCCBoundedLower";RegisterClassW(&cls);
   lower=CreateWindowExW(0,cls.lpszClassName,L"",WS_POPUP|WS_VISIBLE,0,0,800,600,nullptr,nullptr,cls.hInstance,nullptr);need(lower,"Lower HWND creation failed");
   auto create=[&](Carrier& carrier,int id,bool notice=false){
    auto s=std::make_shared<Carrier::State>();s->owner=&carrier;s->id=id;s->nonce="native-bounded-input-0001";s->global=true;s->notice=notice;s->monitor=MonitorFromWindow(lower,MONITOR_DEFAULTTOPRIMARY);
    cls.lpfnWndProc=Carrier::proc;cls.lpszClassName=L"YMCCBoundedDisplay";RegisterClassW(&cls);
    s->hwnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT,cls.lpszClassName,L"",WS_POPUP|WS_DISABLED,0,0,150,90,nullptr,nullptr,cls.hInstance,s.get());need(s->hwnd&&hudinput::ensure(s->hwnd),"Inert display unavailable");
    s->ready=s->armed=s->layout=true;s->width=150;s->height=90;s->seq=1;s->expires=wallNow()+60000;s->viewportKey=Carrier::viewportInfo(*s).dump();s->crosshair=true;s->drag.position={.1,.1,true,false,1};
    auto controller=Microsoft::WRL::Make<NoticeControllerRecorder>();s->controller=controller;carrier.current=s;carrier.refresh(s);return std::pair{s,controller};
   };
   auto [s,record]=create(owner,7);need(s->inputWindow&&s->input.active(),"Bounded native input unavailable");
   auto reset=[&]{owner.stopInteraction(s);s->drag.position.locked=false;s->drag.position.placed=true;s->drag.position.x=.1;s->drag.position.y=.1;++s->drag.position.revision;s->expires=wallNow()+60000;s->armed=s->layout=true;s->visibility=hudvisibility::Mode::Always;owner.refresh(s);};
   auto mouse=[&](UINT m,WPARAM buttons,int x,int y){SendMessageW(s->inputWindow,m,buttons,MAKELPARAM(x,y));};
   auto pointer=[&](UINT msg,UINT32 id,POINTER_INPUT_TYPE type,int dx,int dy,POINTER_FLAGS flags,POINTER_BUTTON_CHANGE_TYPE change=POINTER_CHANGE_NONE,DWORD age=0){
    pointerMetadata={};pointerMetadata.pointerId=id;pointerMetadata.pointerType=type;pointerMetadata.ptPixelLocation={s->panel.left+dx,s->panel.top+dy};pointerMetadata.pointerFlags=flags;pointerMetadata.ButtonChangeType=change;pointerMetadata.dwTime=GetTickCount()-age;pointerMetadataEnabled=true;
    SendMessageW(s->inputWindow,msg,id,MAKELPARAM(pointerMetadata.ptPixelLocation.x,pointerMetadata.ptPixelLocation.y));pointerMetadataEnabled=false;
   };
   auto templateCount=[&]{unsigned n=0;for(const auto& e:events)if(e.value("name",std::string{})=="desktopHud.template")++n;return n;};
   RECT input{},display{};GetWindowRect(s->inputWindow,&input);GetWindowRect(s->hwnd,&display);
   check("full-screen renderer leaves only a panel-sized input HWND",input.right-input.left==150&&input.bottom-input.top==90&&display.right-display.left>150,{{"input",rectangle(input)},{"display",rectangle(display)}});
   int children=0;EnumChildWindows(s->inputWindow,[](HWND,LPARAM p)->BOOL{++*reinterpret_cast<int*>(p);return TRUE;},reinterpret_cast<LPARAM>(&children));
   check("monitor creates no Composition input child",children==0);
   check("monitor does not initialize an independent input runtime",!runtime.compositor);
   check("full-screen monitor renderer is disabled and mouse transparent",hudinput::inert(s->hwnd));
   // A contact ellipse may overlap the HUD while its actual touch point is
   // outside. Rejection must happen during Windows target selection, before
   // discarding an already-targeted WM_POINTERDOWN could swallow the click.
   for(auto point:std::array<POINT,4>{POINT{s->panel.left-1,s->panel.top+25},POINT{s->panel.right,s->panel.top+25},POINT{s->panel.left+30,s->panel.bottom},POINT{s->panel.right+100,s->panel.bottom+100}}){
     TOUCH_HIT_TESTING_INPUT input{};input.pointerId=81;input.point=point;input.boundingBox={point.x-24,point.y-24,point.x+24,point.y+24};input.nonOccludedBoundingBox=input.boundingBox;
     const auto request=reinterpret_cast<LPARAM>(&input);const auto actual=SendMessageW(s->inputWindow,WM_TOUCHHITTESTING,0,request);
     check("touch point outside panel is rejected before pointer targeting",actual==hudinput::rejectTouchHitTest(request),{{"x",point.x},{"y",point.y}});
   }
   for(auto point:std::array<POINT,2>{POINT{s->panel.left,s->panel.top},POINT{s->panel.right-1,s->panel.bottom-1}}){
     TOUCH_HIT_TESTING_INPUT input{};input.pointerId=82;input.point=point;input.boundingBox={point.x-24,point.y-24,point.x+24,point.y+24};input.nonOccludedBoundingBox=input.boundingBox;
     const auto request=reinterpret_cast<LPARAM>(&input);check("touch point inside panel remains eligible",SendMessageW(s->inputWindow,WM_TOUCHHITTESTING,0,request)!=hudinput::rejectTouchHitTest(request));
   }
   const auto initial=s->panel;mouse(WM_LBUTTONDOWN,MK_LBUTTON,30,25);
   check("monitor mouse DOWN never captures the desktop",s->drag.contact()&&GetCapture()!=s->inputWindow);
   mouse(WM_MOUSEMOVE,MK_LBUTTON,54,37);mouse(WM_LBUTTONUP,0,30,25);
   check("native mouse dragging follows the pointer without capture",std::abs(s->panel.left-initial.left-24)<=1&&std::abs(s->panel.top-initial.top-12)<=1&&!s->drag.contact()&&GetCapture()!=s->inputWindow);
   GetWindowRect(s->inputWindow,&input);check("input stays with the actual moved panel",EqualRect(&input,&s->panel));
   reset();const auto switches=templateCount();for(int i=0;i<2;++i){mouse(WM_LBUTTONDOWN,MK_LBUTTON,30,25);mouse(WM_LBUTTONUP,0,30,25);}
   check("mouse double click switches A/B exactly once without locking",templateCount()==switches+1&&!s->drag.position.locked&&!s->drag.contact());
   reset();mouse(WM_LBUTTONDOWN,MK_LBUTTON,30,25);check("missing remote UP cannot leave desktop capture",s->drag.contact()&&GetCapture()!=s->inputWindow);
   mouse(WM_MOUSEMOVE,0,30,25);check("missing button state retires an interrupted press",!s->drag.contact()&&!s->mouseContact);
   reset();mouse(WM_LBUTTONDOWN,MK_LBUTTON,-10,25);check("out-of-panel remote DOWN cannot admit a contact",!s->drag.contact()&&GetCapture()!=s->inputWindow);
   reset();mouse(WM_LBUTTONDOWN,MK_LBUTTON,30,25);SetCapture(lower);SendMessageW(s->inputWindow,WM_CAPTURECHANGED,0,(LPARAM)lower);
   check("cancellation preserves another window's mouse capture",!s->drag.contact()&&GetCapture()==lower);ReleaseCapture();
   for(auto type:{PT_TOUCH,PT_PEN}){
    reset();const auto before=s->panel;pointer(WM_POINTERDOWN,21,type,30,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN);
    check("native pointer DOWN enters the actual consumer without mouse capture",s->drag.contact()&&GetCapture()!=s->inputWindow,{{"type",type}});
    pointer(WM_POINTERUPDATE,21,type,54,37,POINTER_FLAG_UPDATE|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON);
    pointer(WM_POINTERUP,21,type,30,25,POINTER_FLAG_UP,POINTER_CHANGE_FIRSTBUTTON_UP);
    check("native pointer drag moves once and releases",std::abs(s->panel.left-before.left-24)<=1&&std::abs(s->panel.top-before.top-12)<=1&&!s->drag.contact(),{{"type",type}});
    reset();const auto count=templateCount();for(int i=0;i<2;++i){pointer(WM_POINTERDOWN,22,type,30,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN);pointer(WM_POINTERUP,22,type,30,25,POINTER_FLAG_UP,POINTER_CHANGE_FIRSTBUTTON_UP);}
    check("native pointer double tap switches once and never locks",templateCount()==count+1&&!s->drag.position.locked,{{"type",type}});
    reset();pointer(WM_POINTERDOWN,23,type,30,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN);
    SendMessageW(s->inputWindow,WM_POINTERCAPTURECHANGED,24,0);check("unrelated pointer cannot cancel the admitted contact",s->drag.contact(),{{"type",type}});
    SendMessageW(s->inputWindow,WM_POINTERCAPTURECHANGED,23,0);check("matching pointer capture loss cancels without a tap",!s->drag.contact(),{{"type",type}});
    reset();pointer(WM_POINTERDOWN,24,type,-10,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN);check("native pointer outside the panel cannot start dragging",!s->drag.contact(),{{"type",type}});
    reset();pointer(WM_POINTERDOWN,25,type,30,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON,POINTER_CHANGE_FIRSTBUTTON_DOWN);
    pointer(WM_POINTERUPDATE,25,type,30,25,POINTER_FLAG_CANCELED|POINTER_FLAG_UPDATE);check("cancelled pointer cannot retain a gesture",!s->drag.contact(),{{"type",type}});
   }
   for(auto type:{PT_TOUCH,PT_PEN})for(auto failedMessage:{WM_POINTERUP,WM_POINTERUPDATE}){
    reset();const auto count=templateCount();const unsigned beforeSummons=summons;
    pointer(WM_POINTERDOWN,61,type,30,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON);
    pointer(WM_POINTERUPDATE,61,type,54,37,POINTER_FLAG_UPDATE|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON);
    failedPointerMetadataId=62;SendMessageW(s->inputWindow,failedMessage,62,0);failedPointerMetadataId=0;
    check("failed metadata for another pointer leaves the admitted owner intact",s->touchContact&&s->drag.contact(),{{"type",type},{"message",failedMessage}});
    failedPointerMetadataId=61;SendMessageW(s->inputWindow,failedMessage,61,0);failedPointerMetadataId=0;
    check("missing metadata for the active pointer cancels drag and returns contact ownership",!s->touchContact&&!s->drag.contact()&&!s->drag.dragging()&&!s->radialContact&&GetCapture()!=s->inputWindow,{{"type",type},{"message",failedMessage}});
    check("metadata cancellation cannot switch templates or summon the radial",templateCount()==count&&summons==beforeSummons,{{"type",type},{"message",failedMessage}});
    owner.refresh(s); // Reuse the ordinary refresh; cancellation adds no recovery timer.
    pointer(WM_POINTERDOWN,63,type,30,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT|POINTER_FLAG_FIRSTBUTTON);
    check("a new pointer is admitted after metadata cancellation",s->touchContact&&s->drag.contact()&&s->touchPointerId==63,{{"type",type},{"message",failedMessage}});
    pointer(WM_POINTERUP,63,type,30,25,POINTER_FLAG_UP);
   }
   reset();pointer(WM_POINTERDOWN,71,PT_TOUCH,30,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT);
   failedClientConversion=s->inputWindow;pointer(WM_POINTERUP,71,PT_TOUCH,30,25,POINTER_FLAG_UP);failedClientConversion=nullptr;
   check("failed pointer coordinate conversion cancels the known owner",!s->drag.contact()&&!s->touchContact&&!IsWindowVisible(s->inputWindow));
   radial=true;reset();const unsigned beforeFailedHold=summons;
   pointer(WM_POINTERDOWN,72,PT_TOUCH,30,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT,POINTER_CHANGE_NONE,700);SendMessageW(s->hwnd,WM_TIMER,Carrier::holdTimer,0);
   need(s->radialContact&&s->radialArmed,"Fixture must arm its own known long press before metadata loss");
   failedPointerMetadataId=72;SendMessageW(s->inputWindow,WM_POINTERUP,72,0);failedPointerMetadataId=0;
   check("metadata loss retires an armed radial contact and hides its input",!s->drag.contact()&&!s->touchContact&&!s->radialContact&&!s->radialArmed&&!IsWindowVisible(s->inputWindow));
   SendMessageW(s->hwnd,WM_TIMER,Carrier::holdTimer,0);pointer(WM_POINTERUP,72,PT_TOUCH,30,25,POINTER_FLAG_UP);
   check("late timer and UP after metadata loss cannot summon or revive input",summons==beforeFailedHold&&!s->drag.contact()&&!s->radialContact&&!IsWindowVisible(s->inputWindow));
   radial=false;
   reset();s->drag.position.locked=true;++s->drag.position.revision;owner.refresh(s);check("explicit lock hides the interactive panel while display remains visible",!IsWindowVisible(s->inputWindow)&&IsWindowVisible(s->hwnd));
   Carrier::inputEvent(s->inputWindow,{10,41,PT_TOUCH,screeninput::Phase::Down,30,25,false},s.get());check("late input cannot revive a hidden locked panel",!s->drag.contact());
   radial=true;owner.refresh(s);unsigned beforeSummons=summons;
   pointer(WM_POINTERDOWN,43,PT_TOUCH,30,25,POINTER_FLAG_DOWN|POINTER_FLAG_INCONTACT,POINTER_CHANGE_NONE,700);SendMessageW(s->hwnd,WM_TIMER,Carrier::holdTimer,0);
   check("configured radial hold only arms while held",s->radialContact&&summons==beforeSummons);pointer(WM_POINTERUP,43,PT_TOUCH,30,25,POINTER_FLAG_UP);
   check("configured radial hold summons once on matching release",summons==beforeSummons+1&&!s->radialContact&&s->drag.position.locked);
   SendMessageW(s->hwnd,WM_TIMER,Carrier::holdTimer,0);check("late radial timer cannot repeat the action",summons==beforeSummons+1);
   radial=false;reset();
   for(unsigned i=0;i<4;++i){mouse(WM_LBUTTONDOWN,MK_LBUTTON,30,25);
    if(i==0)owner.stopInteraction(s);if(i==1)ShowWindow(s->inputWindow,SW_HIDE);if(i==2)SendMessageW(s->inputWindow,WM_CANCELMODE,0,0);if(i==3){s->expires=wallNow()-1;owner.refresh(s);}
    check("every interrupted-hide path retires the contact and leaves no capture",!s->drag.contact()&&!s->mouseContact&&GetCapture()!=s->inputWindow&&!IsWindowVisible(s->inputWindow),{{"path",i}});reset();
   }
   s->layout=false;owner.refresh(s);check("layout invalidation hides native windows while renderer measures",!IsWindowVisible(s->hwnd)&&!IsWindowVisible(s->inputWindow));s->layout=true;owner.refresh(s);
   s->visibility=hudvisibility::Mode::GameRunning;s->policyGamePid=0;owner.refresh(s);check("game visibility gate withdraws renderer and input",!record->visible&&!IsWindowVisible(s->inputWindow));s->visibility=hudvisibility::Mode::Always;owner.refresh(s);
   const auto uploads=s->input.uploads;for(int i=0;i<50;++i)owner.refresh(s);check("unchanged snapshots never upload the input bitmap again",s->input.uploads==uploads);
   s->width=170;owner.refresh(s);GetWindowRect(s->inputWindow,&input);check("panel resize updates only the bounded input geometry",input.right-input.left==170&&s->input.uploads==uploads+1);
   SetWindowLongPtrW(s->inputWindow,GWL_EXSTYLE,GetWindowLongPtrW(s->inputWindow,GWL_EXSTYLE)|WS_EX_TRANSPARENT);owner.refresh(s);check("event-driven style repair preserves bounded native input",s->input.active());
   for(unsigned i=0;i<2;++i){const HWND old=s->inputWindow;DestroyWindow(old);check("external input destruction withdraws stale input and gesture",!s->inputWindow&&!s->drag.contact());owner.refresh(s);check("next admitted update rebuilds one bounded native input HWND",s->inputWindow&&s->input.active()&&IsWindowVisible(s->inputWindow));}
   {
    Carrier displaced;displaced.initialize(nullptr,cfg);auto [d,drecord]=create(displaced,79);const HWND enlargedInput=d->inputWindow;
    originalPlacementProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(enlargedInput,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(placementProc)));need(originalPlacementProc,"Own placement observer unavailable");
    inflateNextPlacement=true;d->width=170;displaced.refresh(d);
    check("input larger than its visible panel is withdrawn immediately",!d->inputWindow&&!IsWindow(enlargedInput)&&d->inputError=="drag-input-placement-failed");
    bool failureGeometryLogged=false;for(const auto& e:events)if(e.value("name",std::string{})=="desktopHud.inputUnavailable"&&e["value"].value("id",0)==d->id&&e["value"].contains("inputWithdrawal")){
      const auto& w=e["value"]["inputWithdrawal"];failureGeometryLogged=w["inputRect"]["width"]!=w["panelRect"]["width"]&&w.contains("cursorOutsidePanel");}
    check("geometry failure logs its live mismatch before recovery",failureGeometryLogged);
    inflateNextPlacement=false;displaced.refresh(d);RECT repaired{};if(d->inputWindow)GetWindowRect(d->inputWindow,&repaired);
    check("next admitted update restores only the visible panel input bounds",d->inputWindow&&d->input.active()&&EqualRect(&repaired,&d->panel));
    displaced.close(d->id,d->nonce);originalPlacementProc=nullptr;
    check("placement-failure cleanup preserves the other monitor owner",s->inputWindow&&IsWindow(s->inputWindow)&&s->input.active());
   }
   {
    Carrier mutated;mutated.initialize(nullptr,cfg);auto [m,mrecord]=create(mutated,89);const HWND input=m->inputWindow;
    const RECT panel=m->panel;RECT applied{};
    SetWindowPos(input,nullptr,panel.left+260,panel.top+180,m->width+480,m->height+320,SWP_NOZORDER|SWP_NOACTIVATE);
    GetWindowRect(input,&applied);
    check("external input placement cannot escape the panel between refreshes",EqualRect(&applied,&panel),{{"panel",rectangle(panel)},{"actual",rectangle(applied)}});
    SetWindowPos(input,nullptr,0,0,m->width+700,m->height+500,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE);
    GetWindowRect(input,&applied);
    check("external size-only mutation remains bounded before the next refresh",EqualRect(&applied,&panel),{{"panel",rectangle(panel)},{"actual",rectangle(applied)}});
    SetWindowPos(input,nullptr,panel.left+300,panel.top+200,0,0,SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE);
    GetWindowRect(input,&applied);
    check("external move-only mutation remains over the visible panel",EqualRect(&applied,&panel),{{"panel",rectangle(panel)},{"actual",rectangle(applied)}});
    SetWindowPos(input,nullptr,panel.left+400,panel.top+250,m->width+800,m->height+600,SWP_NOSENDCHANGING|SWP_NOZORDER|SWP_NOACTIVATE);
    check("placement bypass immediately withdraws the input window without a telemetry refresh",!m->inputWindow&&!IsWindow(input)&&m->input.terminalReleased()&&m->inputError=="drag-input-placement-failed");
    mutated.refresh(m);RECT repaired{};if(m->inputWindow)GetWindowRect(m->inputWindow,&repaired);
    check("placement bypass recovery restores only current panel bounds",m->inputWindow&&EqualRect(&repaired,&m->panel)&&m->input.active());
    mutated.close(m->id,m->nonce);check("external mutation cleanup preserves the other monitor owner",s->inputWindow&&IsWindow(s->inputWindow)&&s->input.active());
   }
   {
    Carrier movedDisplay;movedDisplay.initialize(nullptr,cfg);auto [d,drecord]=create(movedDisplay,90);d->crosshair=false;movedDisplay.refresh(d);
    const HWND oldInput=d->inputWindow;const RECT oldPanel=d->panel;
    SendMessageW(oldInput,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(30,25));
    SetWindowPos(d->hwnd,nullptr,oldPanel.left+260,oldPanel.top+160,d->width,d->height,SWP_NOZORDER|SWP_NOACTIVATE);
    check("independent display movement immediately withdraws the old input area",!IsWindowVisible(oldInput)&&!d->drag.contact()&&!d->mouseContact&&GetCapture()!=oldInput);
    movedDisplay.refresh(d);RECT actualDisplay{},actualInput{};GetWindowRect(d->hwnd,&actualDisplay);if(d->inputWindow)GetWindowRect(d->inputWindow,&actualInput);
    check("next update restores aligned display and input after independent movement",d->inputWindow&&IsWindowVisible(d->inputWindow)&&EqualRect(&actualDisplay,&d->panel)&&EqualRect(&actualInput,&d->panel));
    originalPlacementProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(d->hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(placementProc)));need(originalPlacementProc,"Display placement observer unavailable");
    inflateNextPlacement=true;++d->width;movedDisplay.refresh(d);
    check("altered display placement cannot rearm a detached input area",!IsWindowVisible(d->hwnd)&&!IsWindowVisible(d->inputWindow)&&!d->drag.contact()&&d->reason=="display-placement-failed");
    SetWindowLongPtrW(d->hwnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(originalPlacementProc));originalPlacementProc=nullptr;inflateNextPlacement=false;movedDisplay.refresh(d);
    GetWindowRect(d->hwnd,&actualDisplay);if(d->inputWindow)GetWindowRect(d->inputWindow,&actualInput);
    check("ordinary frame recovers only an aligned display and input pair",IsWindowVisible(d->hwnd)&&d->inputWindow&&IsWindowVisible(d->inputWindow)&&EqualRect(&actualDisplay,&d->panel)&&EqualRect(&actualInput,&d->panel));
    const HWND display=d->hwnd,input=d->inputWindow;SendMessageW(input,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(30,25));DestroyWindow(display);
    check("external display destruction retires its input contact and controller",!IsWindow(display)&&!IsWindow(input)&&!d->inputWindow&&!d->drag.contact()&&!d->mouseContact&&drecord->closed&&!movedDisplay.current);
    movedDisplay.refresh(d);
    check("late refresh cannot recreate an externally destroyed display owner",!movedDisplay.current&&!d->hwnd&&!d->inputWindow);
    check("display loss leaves the unrelated monitor active",s->inputWindow&&s->input.active()&&IsWindowVisible(s->inputWindow));
   }
   {
    Carrier notices;Config noticeCfg=cfg;noticeCfg.noticeOnly=true;notices.initialize(nullptr,noticeCfg);auto [n,nrecord]=create(notices,91,true);
    check("notice renderer creates no interactive input window",!n->inputWindow&&hudinput::inert(n->hwnd));
    notices.noticeHide(n->id,n->nonce,n->seq);check("hidden notice suspends renderer without a click target",!nrecord->visible&&!IsWindowVisible(n->hwnd)&&!n->inputWindow);
    const HWND window=n->hwnd;notices.close(n->id,n->nonce);check("notice close hides controller and destroys HWND",nrecord->closed&&!nrecord->visibleAtClose&&!IsWindow(window)&&!notices.current);
   }
   // Inject capture into our private test HWND only. Product must record the
   // live owner before cancellation releases it, then preserve that evidence.
   reset();mouse(WM_LBUTTONDOWN,MK_LBUTTON,30,25);SetCapture(s->inputWindow);owner.stopInteraction(s);
   check("withdrawal evidence captures the live owner before releasing input",inputWithdrawalState(*s).is_object()&&inputWithdrawalState(*s).value("captureIsInput",false)&&inputWithdrawalState(*s).value("mouseContact",false)&&inputWithdrawalState(*s).value("inputVisible",false)&&GetCapture()!=s->inputWindow);
   const auto withdrawal=inputWithdrawalState(*s);for(int i=0;i<20;++i)owner.refresh(s); // May reshow the retained HWND; does not withdraw it.
   check("ordinary snapshots never overwrite withdrawal evidence",inputWithdrawalState(*s)==withdrawal);
   owner.stopInteraction(s);const auto hiddenWithdrawal=inputWithdrawalState(*s);for(int i=0;i<20;++i)owner.stopInteraction(s);
   check("repeated hidden cleanup does not erase the previous live input state",inputWithdrawalState(*s)==hiddenWithdrawal);
   const auto failedDiagnostic=[&]<class State>(const std::shared_ptr<State>& state){if constexpr(requires{state->inputWithdrawal;}){
     reset();mouse(WM_LBUTTONDOWN,MK_LBUTTON,30,25);SetCapture(state->inputWindow);const auto previous=inputWithdrawalState(*state);failNextAllocation=true;owner.stopInteraction(state);
     check("diagnostic allocation failure never blocks actual input release",!failNextAllocation&&!state->drag.contact()&&!IsWindowVisible(state->inputWindow)&&GetCapture()!=state->inputWindow&&inputWithdrawalState(*state)==previous);
   }};failedDiagnostic(s);
   const HWND displayWindow=s->hwnd,inputWindow=s->inputWindow;owner.close(s->id,s->nonce);check("monitor close destroys both HWNDs and closes renderer",!IsWindow(displayWindow)&&!IsWindow(inputWindow)&&record->closed&&!record->visibleAtClose&&!owner.current);
   bool closedEvidence=false;for(const auto& event:events)if(event.value("name",std::string{})=="desktopHud.closed"&&event["value"].value("id",0)==s->id)closedEvidence=event["value"].contains("inputWithdrawal")&&event["value"]["inputWithdrawal"].value("inputMode",std::string{})=="native-panel-no-capture-v1";
   check("ordinary close lifecycle event retains bounded input evidence",closedEvidence);
   bool releasedEvidence=false;for(const auto& event:events)if(event.value("name",std::string{})=="desktopHud.closed"&&event["value"].value("id",0)==s->id&&event["value"].contains("inputRelease")){
     const auto& r=event["value"]["inputRelease"];releasedEvidence=r.value("inputDestroyed",false)&&r.value("displayDestroyed",false)&&r.value("surfaceReleased",false)&&r.value("controllerReleased",false)&&r.value("contactsCleared",false)&&!r.value("captureRetained",true);
   }
   check("close lifecycle proves both windows and input terminal were released",releasedEvidence);

   check("monitor lifetime never created an independent input runtime",!runtime.compositor);
   DestroyWindow(lower);lower=nullptr;
  }
 }catch(const winrt::hresult_error& e){ok=false;error="WinRT:"+std::to_string((long)e.code());}catch(const std::exception& e){ok=false;error=e.what();}
 if(lower)DestroyWindow(lower);if(com)winrt::uninit_apartment();if(desk){SetThreadDesktop(original);CloseDesktop(desk);}
 const Json result={{"ok",ok},{"cases",cases},{"error",error},{"events",events},{"systemInputInjected",false},{"privateDesktop",true},{"visibleDesktopSwitched",false},{"physicalHaptics",false},{"scope","Actual production carrier on owned private HWNDs; native messages, pointer metadata recorder, COM controller recorder. No physical/UU claim."}};
 std::ofstream(argv[1])<<result.dump(2);std::cout<<Json{{"ok",ok},{"cases",cases.size()},{"error",error}}.dump()<<std::endl;return ok?0:3;
}
