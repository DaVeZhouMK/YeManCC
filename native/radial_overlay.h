#pragma once
// YMCC-owned, windowless wheel. Uses the same WebView2 environment and controlled
// PointerPoint source as the monitor; no hook, device reader or plugin host.
#include "monitor_desktop_hud.h"
#include "radial_input_region.h"
#include "radial_haptics.h"
#include <Xinput.h>
#include <atomic>
namespace ymcc::radial {
using Json=nlohmann::json;
using Microsoft::WRL::ComPtr;using Microsoft::WRL::Callback;
using desktophud::wide;using desktophud::utf8;
struct Config {
 std::function<bool(ICoreWebView2*)> serve;
 std::function<void(const std::string&,const Json&)> notify;
 std::function<void(bool)> ownership;
 std::function<void(bool,bool)> haptic;
 screeninput::Runtime* inputRuntime=nullptr;HWND anchor=nullptr;std::function<void(bool)> visibility;
 std::function<int()> hapticStrength;
};
class Overlay {
#ifdef YMCC_RADIAL_NATIVE_TEST
 friend struct OverlayTestAccess; // Isolated native/renderer consumer harness only.
#endif
 static constexpr UINT expireTimer=0x5952,transitionTimer=0x5953,closeMessage=WM_APP+194;
 static constexpr unsigned enterMs=240,exitMs=200,exitFallbackMs=350;
 struct State {
  Overlay* owner=nullptr;HWND window=nullptr,inputWindow=nullptr;int id=0;std::string nonce,url;
  bool ready=false,closed=false,closing=false,neutral=false;ULONGLONG deadline=0,lastPad=0;WORD previous=0;
  TouchFeedback touchFeedback;WORD direction=0;ULONGLONG directionStarted=0;
  enum class Control {Stick,Navigation,Pointer};Control control=Control::Stick;
  float previousX=0,previousY=0,claimX=0,claimY=0;bool stickWasNeutral=true;Json payload;RECT rect{};double scale=1;
  screeninput::Surface input;ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2> view;
  ComPtr<ICoreWebView2CompositionController> composition;ComPtr<IDCompositionDevice> device;
  ComPtr<IDCompositionTarget> target;ComPtr<IDCompositionVisual> visual;
 };
 Config config;ComPtr<ICoreWebView2Environment> environment;std::shared_ptr<State> current;int nextId=1;
 bool owns(const std::shared_ptr<State>& s)const{return current==s&&!s->closed;}
 void send(const std::shared_ptr<State>& s,const std::string& event,const Json& data){if(owns(s)&&s->view)s->view->PostWebMessageAsJson(wide(Json{{"event",event},{"data",data}}.dump()).c_str());}
 static LRESULT CALLBACK inputProc(HWND h,UINT m,WPARAM w,LPARAM l){
  if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_POINTERACTIVATE)return PA_NOACTIVATE;
  if(m==WM_POINTERDOWN||m==WM_POINTERUPDATE||m==WM_POINTERUP)return 0;
  if(m==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);return 0;}if(m==WM_ERASEBKGND)return 1;
  return DefWindowProcW(h,m,w,l);
 }
 static void pointer(HWND,const screeninput::Event& e,void* ctx){
  auto* raw=static_cast<State*>(ctx);if(!raw||!raw->owner||raw->closed)return;auto s=raw->owner->current;if(!s||s.get()!=raw||!s->ready||s->closing)return;
  if(!e.cancelled&&(e.phase==screeninput::Phase::Down||e.phase==screeninput::Phase::Move)){
   s->control=State::Control::Pointer;s->claimX=s->previousX;s->claimY=s->previousY;s->stickWasNeutral=std::hypot(s->previousX,s->previousY)<.26f;
  }
  if(e.type==PT_TOUCH||e.type==PT_PEN){
   const auto tick=GetTickCount64(),at=e.timeKnown?tick-(DWORD)((DWORD)tick-e.sourceTime):tick;
   const auto phase=e.cancelled||e.phase==screeninput::Phase::Cancel?TouchFeedback::Phase::Cancel:
     e.phase==screeninput::Phase::Down?TouchFeedback::Phase::Down:e.phase==screeninput::Phase::Move?TouchFeedback::Phase::Move:TouchFeedback::Phase::Up;
   const int strength=raw->owner->config.hapticStrength?raw->owner->config.hapticStrength():touchhaptic::kMediumStrengthPercent;
   const auto feedback=s->touchFeedback.event(s->payload.value("feedback",false),strength,phase,e.id,e.x,e.y,
     s->rect.right-s->rect.left,s->rect.bottom-s->rect.top,at);
   if(raw->owner->config.haptic)raw->owner->config.haptic(feedback.contact,feedback.pulse);
  }
  std::string phase=e.phase==screeninput::Phase::Down?"down":e.phase==screeninput::Phase::Move?"move":"up";
  raw->owner->send(s,"radial.pointer",{{"id",s->id},{"nonce",s->nonce},{"contact",e.id},{"phase",phase},{"x",e.x/s->scale},{"y",e.y/s->scale},{"cancelled",e.cancelled||e.phase==screeninput::Phase::Cancel}});
 }
 void region(const std::shared_ptr<State>& s,const Json& polygons){
  if(s->closing)return;
  HRGN joined=makeInputRegion(polygons,s->scale);
  if(!SetWindowRgn(s->inputWindow,joined,FALSE)){DeleteObject(joined);throw std::runtime_error("Wheel input region failed");}
  if(!s->input.hasContacts()&&!s->input.sync())throw std::runtime_error("Wheel input sync failed");
  ShowWindow(s->inputWindow,SW_SHOWNOACTIVATE);
 }
 void receive(const std::shared_ptr<State>& s,ICoreWebView2WebMessageReceivedEventArgs* args){
  if(!owns(s))return;LPWSTR source=nullptr,raw=nullptr;if(FAILED(args->get_Source(&source))||!source)return;
  bool valid=std::wstring(source)==wide(s->url);CoTaskMemFree(source);if(!valid)return;
  if(FAILED(args->get_WebMessageAsJson(&raw))||!raw)return;std::wstring text(raw);CoTaskMemFree(raw);if(text.size()>65536)return;
  Json response;try{const auto request=Json::parse(utf8(text));response["id"]=request.value("id",-1);const auto a=request.value("args",Json::object());const auto cmd=request.value("cmd",std::string{});
   if(a.value("wheelId",0)!=s->id||a.value("nonce",std::string{})!=s->nonce)throw std::runtime_error("Wheel owner mismatch");
   if(s->closing&&cmd!="radial.close"&&cmd!="radial.transition.complete")throw std::runtime_error("Wheel is closing");
   if(cmd=="radial.ready"){s->ready=true;response["result"]={{"id",s->id},{"nonce",s->nonce}};send(s,"radial.frame",s->payload);if(config.visibility)config.visibility(true);ShowWindow(s->window,SW_SHOWNOACTIVATE);}
   else if(cmd=="radial.regions"){region(s,a.at("polygons"));response["result"]=true;}
   else if(cmd=="radial.intent"){
    const auto intent=a.value("intent",Json::object());if(!intent.is_object()||intent.dump().size()>2048)throw std::runtime_error("Invalid wheel intent");
    if(config.notify)config.notify("radial.intent",{{"wheelId",s->id},{"nonce",s->nonce},{"intent",intent}});response["result"]=true;
   }else if(cmd=="radial.feedback"){if(config.haptic&&s->payload.value("feedback",false)&&!s->touchFeedback.contacting())config.haptic(true,true);response["result"]=true;}
   else if(cmd=="radial.close"){response["result"]=dismiss(s->id,s->nonce);}
   else if(cmd=="radial.transition.complete"){
    if(!s->closing||a.value("phase",std::string{})!="closed")throw std::runtime_error("Invalid wheel transition");
    response["result"]=true;if(s->view)s->view->PostWebMessageAsJson(wide(response.dump()).c_str());close(s->id,s->nonce,"animated-close");return;
   }
   else throw std::runtime_error("Wheel command denied");
  }catch(const std::exception&e){response["error"]=e.what();}if(s->view)s->view->PostWebMessageAsJson(wide(response.dump()).c_str());
 }
 static LRESULT CALLBACK proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){
  auto* s=reinterpret_cast<State*>(GetWindowLongPtrW(hwnd,GWLP_USERDATA));if(msg==WM_NCCREATE){s=static_cast<State*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(hwnd,GWLP_USERDATA,(LONG_PTR)s);}
  if(s&&s->owner){
   if(msg==WM_TIMER&&w==expireTimer){if(GetTickCount64()>s->deadline)s->owner->close(s->id,s->nonce,"owner-lease-expired");return 0;}
   if(msg==WM_TIMER&&w==transitionTimer){s->owner->close(s->id,s->nonce,"close-animation-timeout");return 0;}
   if(msg==WM_POWERBROADCAST&&(w==PBT_APMSUSPEND)){s->owner->close(s->id,s->nonce,"suspend");return TRUE;}
   if(msg==WM_CLOSE){s->owner->dismiss(s->id,s->nonce);return 0;}
   if(msg==closeMessage||msg==WM_DISPLAYCHANGE||msg==WM_DPICHANGED){s->owner->close(s->id,s->nonce,"window-close");return 0;}
   if(msg==WM_NCHITTEST)return HTTRANSPARENT;if(msg==WM_POINTERACTIVATE)return PA_NOACTIVATE;if(msg==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
   if(msg==WM_ERASEBKGND)return 1;if(msg==WM_PAINT){PAINTSTRUCT p;BeginPaint(hwnd,&p);EndPaint(hwnd,&p);return 0;}
  }return DefWindowProcW(hwnd,msg,w,l);
 }
public:
 void initialize(ICoreWebView2Environment* env,Config c){environment=env;config=std::move(c);}
 bool active()const{return current&&!current->closed;}
 Json create(const std::string& nonce,const Json& payload,const std::string& source,int x=0,int y=0){
  if(active())throw std::runtime_error("A wheel already owns input");if(!environment||!config.serve||!config.inputRuntime||!desktophud::tokenValid(nonce))throw std::runtime_error("Wheel runtime unavailable");
  auto s=std::make_shared<State>();s->owner=this;s->nonce=nonce;s->id=nextId++;s->payload=payload;s->deadline=GetTickCount64()+12000;
  const bool pointAnchored=source=="monitor";
  const POINT anchor{x,y};const auto monitor=pointAnchored?MonitorFromPoint(anchor,MONITOR_DEFAULTTONEAREST):MonitorFromWindow(config.anchor,MONITOR_DEFAULTTOPRIMARY);
  MONITORINFO info{sizeof(info)};if(!GetMonitorInfoW(monitor,&info))throw std::runtime_error("Wheel work area unavailable");const auto a=info.rcWork;
  s->scale=std::min({1.0,(a.right-a.left-24)/496.0,(a.bottom-a.top-24)/584.0});if(s->scale<.35)throw std::runtime_error("Wheel work area too small");
  const int width=(int)std::ceil(496*s->scale),height=(int)std::ceil(584*s->scale);RECT main{};GetWindowRect(config.anchor,&main);
  int left=pointAnchored?x-width/2:((main.left+main.right)/2>(a.left+a.right)/2?a.left+12:a.right-width-12);
  int top=pointAnchored?y-height/2:a.top+(a.bottom-a.top-height)/2;left=std::clamp(left,(int)a.left,(int)a.right-width);top=std::clamp(top,(int)a.top,(int)a.bottom-height);s->rect={left,top,left+width,top+height};
  s->payload["transition"]={{"enterMs",enterMs},{"exitMs",exitMs},{"source",source}};
  s->payload["transitionOrigin"]={{"x",pointAnchored?(x-left)/s->scale-248:0},{"y",pointAnchored?(y-top)/s->scale-248:0}};
  s->url="https://app.localhost/radial-output.html?wheelId="+std::to_string(s->id)+"&nonce="+nonce;
  WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=proc;wc.lpszClassName=L"YMCC_RadialOutput";if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("Wheel class unavailable");
  s->window=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_LAYERED|WS_EX_NOREDIRECTIONBITMAP|WS_EX_TRANSPARENT,wc.lpszClassName,L"YMCC Radial",WS_POPUP|WS_DISABLED,left,top,width,height,nullptr,nullptr,wc.hInstance,s.get());
  if(!s->window)throw std::runtime_error("Wheel window unavailable");current=s;
  wc.lpfnWndProc=inputProc;wc.lpszClassName=L"YMCC_RadialInput";RegisterClassW(&wc);
  s->inputWindow=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,wc.lpszClassName,L"",WS_POPUP,left,top,width,height,nullptr,nullptr,wc.hInstance,nullptr);
  if(!s->inputWindow||!s->input.attach(*config.inputRuntime,s->inputWindow,10,pointer,s.get(),true,true)){close(s->id,nonce,"input-unavailable");throw std::runtime_error("Wheel touch input unavailable");}
  if(config.ownership)config.ownership(true);SetTimer(s->window,expireTimer,1000,nullptr);
  ComPtr<ICoreWebView2Environment3> env3;if(FAILED(environment.As(&env3))){close(s->id,nonce,"composition-unavailable");throw std::runtime_error("Wheel composition unavailable");}
  const HRESULT hr=env3->CreateCoreWebView2CompositionController(s->window,Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>([this,s](HRESULT hr,ICoreWebView2CompositionController* composition)->HRESULT{
   ComPtr<ICoreWebView2Controller> controller;if(composition)composition->QueryInterface(IID_PPV_ARGS(&controller));if(!owns(s)){if(controller)controller->Close();return S_OK;}
   const auto failure=[&]{PostMessageW(s->window,closeMessage,0,0);return S_OK;};if(FAILED(hr)||!controller)return failure();s->controller=controller;s->composition=composition;
   if(FAILED(DCompositionCreateDevice(nullptr,IID_PPV_ARGS(&s->device)))||FAILED(s->device->CreateTargetForHwnd(s->window,TRUE,&s->target))||FAILED(s->device->CreateVisual(&s->visual))||FAILED(s->target->SetRoot(s->visual.Get()))||FAILED(composition->put_RootVisualTarget(s->visual.Get()))||FAILED(s->device->Commit()))return failure();
   if(FAILED(controller->get_CoreWebView2(&s->view))||!s->view)return failure();RECT bounds{};GetClientRect(s->window,&bounds);controller->put_Bounds(bounds);
   ComPtr<ICoreWebView2Controller2> c2;ComPtr<ICoreWebView2Controller3> c3;if(FAILED(controller.As(&c2))||FAILED(controller.As(&c3))||FAILED(c2->put_DefaultBackgroundColor(COREWEBVIEW2_COLOR{0,0,0,0}))||FAILED(c3->put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE_USE_RAW_PIXELS))||FAILED(c3->put_ShouldDetectMonitorScaleChanges(FALSE))||FAILED(c3->put_RasterizationScale(1.0))||!config.serve(s->view.Get()))return failure();
   ComPtr<ICoreWebView2Settings> settings;s->view->get_Settings(&settings);settings->put_AreDefaultContextMenusEnabled(FALSE);settings->put_AreDevToolsEnabled(FALSE);settings->put_IsZoomControlEnabled(FALSE);controller->put_ZoomFactor(1);controller->put_IsVisible(TRUE);
   EventRegistrationToken token{};s->view->add_WebMessageReceived(Callback<ICoreWebView2WebMessageReceivedEventHandler>([this,s](ICoreWebView2*,ICoreWebView2WebMessageReceivedEventArgs*a)->HRESULT{receive(s,a);return S_OK;}).Get(),&token);
   s->view->add_NavigationStarting(Callback<ICoreWebView2NavigationStartingEventHandler>([s](ICoreWebView2*,ICoreWebView2NavigationStartingEventArgs*a)->HRESULT{LPWSTR uri=nullptr;if(SUCCEEDED(a->get_Uri(&uri))&&uri){const bool valid=std::wstring(uri)==wide(s->url);CoTaskMemFree(uri);if(!valid)a->put_Cancel(TRUE);}return S_OK;}).Get(),&token);
   s->view->add_ProcessFailed(Callback<ICoreWebView2ProcessFailedEventHandler>([s](ICoreWebView2*,ICoreWebView2ProcessFailedEventArgs*)->HRESULT{if(s->window)PostMessageW(s->window,closeMessage,0,0);return S_OK;}).Get(),&token);
   if(FAILED(s->view->Navigate(wide(s->url).c_str())))return failure();return S_OK;
  }).Get());
  if(FAILED(hr)){close(s->id,nonce,"create-failed");throw std::runtime_error("Wheel renderer creation failed");}return {{"id",s->id},{"nonce",s->nonce},{"rect",{{"left",left},{"top",top},{"width",width},{"height",height}}}};
 }
 bool update(int id,const std::string& nonce,const Json& payload){auto s=current;if(!s||s->id!=id||s->nonce!=nonce)return false;const auto origin=s->payload.value("transitionOrigin",Json::object()),transition=s->payload.value("transition",Json::object());if(s->closing)return false;s->payload=payload;s->payload["transitionOrigin"]=origin;s->payload["transition"]=transition;if(s->ready)send(s,"radial.frame",s->payload);return true;}
 bool renew(int id,const std::string& nonce){auto s=current;if(!s||s->id!=id||s->nonce!=nonce)return false;s->deadline=GetTickCount64()+7000;return true;}
 bool result(int id,const std::string& nonce,const Json& result){auto s=current;if(!s||s->id!=id||s->nonce!=nonce)return false;send(s,"radial.result",result);return true;}
 void pad(float lx,float ly,WORD buttons,BYTE lt,BYTE rt,ULONGLONG now){auto s=current;if(!s||!s->ready||s->closing)return;
  const auto magnitude=std::hypot(lx,ly),oldMagnitude=std::hypot(s->previousX,s->previousY);
  const bool changed=lx!=s->previousX||ly!=s->previousY;
  s->previousX=lx;s->previousY=ly;
  // The summoning combination is released before this session admits actions.
  if(!s->neutral){s->neutral=buttons==0&&lt<30&&rt<30&&magnitude<.26f;s->previous=buttons;return;}
  const WORD pressed=buttons&~s->previous;s->previous=buttons;
  const auto action=[&](const char* a){send(s,"radial.gamepad",{{"action",a}});};
  const auto cancelDirection=[&]{if(s->direction){action("direction-cancel");s->direction=0;}};
  if(pressed&XINPUT_GAMEPAD_B){cancelDirection();action("back");return;}
  constexpr WORD mask=XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN;
  const WORD navigation=pressed&mask,direction=buttons&mask;
  if(navigation){s->control=State::Control::Navigation;s->claimX=lx;s->claimY=ly;s->stickWasNeutral=magnitude<.26f;}
  else if(s->control!=State::Control::Stick){
   if(magnitude<.26f)s->stickWasNeutral=true;
   if(magnitude>=.26f&&(s->stickWasNeutral||std::hypot(lx-s->claimX,ly-s->claimY)>.16f)){cancelDirection();s->control=State::Control::Stick;}
  }
  const bool due=now>=s->lastPad&&now-s->lastPad>=100;
  if(pressed&(XINPUT_GAMEPAD_LEFT_SHOULDER|XINPUT_GAMEPAD_RIGHT_SHOULDER)){cancelDirection();if(pressed&XINPUT_GAMEPAD_LEFT_SHOULDER)action("page-prev");if(pressed&XINPUT_GAMEPAD_RIGHT_SHOULDER)action("page-next");}
  // Apply selection before A. Neutral analog cancels only its own selection.
  if(!navigation&&s->control==State::Control::Stick&&(due||(pressed&XINPUT_GAMEPAD_A)||(oldMagnitude>=.26f&&magnitude<.26f))&&(changed||magnitude>=.26f))send(s,"radial.gamepad",{{"lx",lx},{"ly",ly}});
  if(pressed&XINPUT_GAMEPAD_A)cancelDirection();
  if(pressed&XINPUT_GAMEPAD_A){} // Explicit A supersedes a simultaneous direction release.
  else if(s->control!=State::Control::Navigation)cancelDirection();
  else if(direction!=s->direction||due){
   const WORD active=direction?direction:s->direction;
   const int x=((active&XINPUT_GAMEPAD_DPAD_RIGHT)?1:0)-((active&XINPUT_GAMEPAD_DPAD_LEFT)?1:0);
   const int y=((active&XINPUT_GAMEPAD_DPAD_DOWN)?1:0)-((active&XINPUT_GAMEPAD_DPAD_UP)?1:0);
   if(x==0&&y==0){cancelDirection();}
   else if(direction!=s->direction){
    if(!direction){if(s->direction)send(s,"radial.gamepad",{{"action","direction"},{"phase","release"},{"x",x},{"y",y},{"heldMs",now>=s->directionStarted?now-s->directionStarted:0}});s->direction=0;}
    else if(navigation||s->direction){cancelDirection();s->direction=direction;s->directionStarted=now;send(s,"radial.gamepad",{{"action","direction"},{"phase","down"},{"x",x},{"y",y},{"heldMs",0}});}
   }else if(direction)send(s,"radial.gamepad",{{"action","direction"},{"phase","hold"},{"x",x},{"y",y},{"heldMs",now>=s->directionStarted?now-s->directionStarted:0}});
  }
  if(due){if(lt>100)action("value-down");if(rt>100)action("value-up");s->lastPad=now;}
  if(pressed&XINPUT_GAMEPAD_A){cancelDirection();action("accept");}
 }

 bool dismiss(int id,const std::string& nonce){
  auto s=current;if(!s)return true;if(s->id!=id||s->nonce!=nonce)return false;
  if(s->closing)return true;if(!s->ready)return close(id,nonce,"close-before-ready");
  s->closing=true;s->touchFeedback.cancel();s->input.cancel();if(s->inputWindow)ShowWindow(s->inputWindow,SW_HIDE);
  if(config.haptic)config.haptic(false,false);
  // Retain ownership and renderer until its exit animation acknowledges completion.
  send(s,"radial.closing",{{"wheelId",id},{"nonce",nonce},{"durationMs",exitMs}});
  if(!SetTimer(s->window,transitionTimer,exitFallbackMs,nullptr))return close(id,nonce,"close-timer-unavailable");
  return true;
 }
 bool close(int id,const std::string& nonce,const std::string& reason="close"){
  auto s=current;if(!s)return true;if(s->id!=id||s->nonce!=nonce)return false;s->closed=true;s->touchFeedback.cancel();
  s->input.cancel();s->input.close();if(s->inputWindow)DestroyWindow(s->inputWindow);s->inputWindow=nullptr;
  if(config.haptic)config.haptic(false,false);if(config.ownership)config.ownership(false);if(config.visibility)config.visibility(false);
  if(s->window){KillTimer(s->window,expireTimer);KillTimer(s->window,transitionTimer);ShowWindow(s->window,SW_HIDE);}if(s->controller)s->controller->Close();
  s->view.Reset();s->controller.Reset();s->composition.Reset();s->visual.Reset();s->target.Reset();s->device.Reset();if(s->window)DestroyWindow(s->window);s->window=nullptr;s->owner=nullptr;current.reset();
  if(config.notify)config.notify("radial.closed",{{"wheelId",id},{"nonce",nonce},{"reason",reason}});return true;
 }
 void shutdown(){if(current)close(current->id,current->nonce,"shutdown");environment.Reset();}~Overlay(){shutdown();}
};
}
