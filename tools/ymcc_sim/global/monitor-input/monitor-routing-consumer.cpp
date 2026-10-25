#define NOMINMAX
#include <windows.h>
static UINT32 failedLivePointerId=0;
static unsigned livePointerMetadataFailures=0;
static BOOL liveFixtureGetPointerInfo(UINT32 id,POINTER_INFO* value){
 if(failedLivePointerId&&id==failedLivePointerId){++livePointerMetadataFailures;SetLastError(ERROR_NOT_FOUND);return FALSE;}
 return GetPointerInfo(id,value);
}
#define GetPointerInfo liveFixtureGetPointerInfo
#include "monitor_desktop_hud.test.h"
#undef GetPointerInfo
#include <WebView2EnvironmentOptions.h>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace ymcc;using namespace ymcc::desktophud;
struct SharedReceiver {volatile LONG ready=0,mouseDown=0,mouseUp=0,touchDown=0,touchUp=0;UINT64 hwnd=0;DWORD pid=0;};
static SharedReceiver* sharedReceiver=nullptr;
static HWND receiver=nullptr;static unsigned mouseDown=0,mouseUp=0,touchDown=0,touchUp=0,injected=0;
static Json lastCursorDelivery=Json::object();
static POINT expectedCursor{},initialCursor{};static bool cursorKnown=false,mouseHeld=false,touchHeld=false;static POINT lastTouch{};
static std::filesystem::path inputMarker;
static void markMouse(bool held){mouseHeld=held;if(held){std::ofstream(inputMarker)<<GetCurrentProcessId();}else if(!inputMarker.empty())DeleteFileW(inputMarker.c_str());}
static WNDPROC inputPlacementOriginal=nullptr;static bool enlargeNextInputPlacement=false;
static LRESULT CALLBACK alteredInputPlacement(HWND h,UINT m,WPARAM w,LPARAM l){const auto result=CallWindowProcW(inputPlacementOriginal,h,m,w,l);if(m==WM_WINDOWPOSCHANGING&&enlargeNextInputPlacement){auto* p=reinterpret_cast<WINDOWPOS*>(l);if(!(p->flags&SWP_NOSIZE)){p->cx+=200;enlargeNextInputPlacement=false;}}return result;}
static Json pointers=Json::array();static bool omitOwnedUp=false,omitReleaseState=false;static unsigned omittedUps=0,omittedReleaseStates=0;
static LRESULT CALLBACK receive(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_POINTERACTIVATE)return PA_NOACTIVATE;
 if(m==WM_POINTERDOWN||m==WM_POINTERUP){POINTER_INFO p{};if(GetPointerInfo(GET_POINTERID_WPARAM(w),&p)){
  pointers.push_back({{"message",m},{"type",p.pointerType},{"x",p.ptPixelLocation.x},{"y",p.ptPixelLocation.y}});
  if(p.pointerType==PT_TOUCH){if(m==WM_POINTERDOWN){++touchDown;if(sharedReceiver)InterlockedIncrement(&sharedReceiver->touchDown);}else{++touchUp;if(sharedReceiver)InterlockedIncrement(&sharedReceiver->touchUp);}}else if(p.pointerType==PT_MOUSE){if(m==WM_POINTERDOWN){++mouseDown;if(sharedReceiver)InterlockedIncrement(&sharedReceiver->mouseDown);}else{++mouseUp;if(sharedReceiver)InterlockedIncrement(&sharedReceiver->mouseUp);}}}
  return 0;}
 if(m==WM_LBUTTONDOWN){++mouseDown;if(sharedReceiver)InterlockedIncrement(&sharedReceiver->mouseDown);return 0;}if(m==WM_LBUTTONUP){++mouseUp;if(sharedReceiver)InterlockedIncrement(&sharedReceiver->mouseUp);return 0;}
 if(m==WM_PAINT){PAINTSTRUCT p;auto dc=BeginPaint(h,&p);RECT r{};GetClientRect(h,&r);FillRect(dc,&r,(HBRUSH)GetStockObject(DKGRAY_BRUSH));SetTextColor(dc,RGB(240,240,240));SetBkMode(dc,TRANSPARENT);DrawTextW(dc,L"YMCC owned input routing test",-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);EndPaint(h,&p);return 0;}
 if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
 return DefWindowProcW(h,m,w,l);
}
static void need(bool b,const char* m){if(!b)throw std::runtime_error(std::string(m)+" win32="+std::to_string(GetLastError()));}
template<class T> static void drainIndependent(T* c){if constexpr(requires{c->current->input.source();})if(c&&c->current&&c->current->input.source())c->current->input.source().Dispatcher().ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);}
static void pump(Carrier* c,DWORD duration){const auto end=GetTickCount64()+duration;do{MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){bool omit=false;if((omitOwnedUp||omitReleaseState)&&c&&c->current&&(m.hwnd==c->current->inputWindow||IsChild(c->current->inputWindow,m.hwnd))){
 if(omitOwnedUp&&(m.message==WM_LBUTTONUP||m.message==WM_POINTERUP))omit=true;
 if(omitReleaseState&&(m.message==WM_CAPTURECHANGED||m.message==WM_POINTERCAPTURECHANGED||m.message==WM_MOUSELEAVE||(m.message==WM_MOUSEMOVE&&!(m.wParam&MK_LBUTTON)))){omit=true;++omittedReleaseStates;}
 if(m.message==WM_POINTERUPDATE){POINTER_INFO p{};if(GetPointerInfo(GET_POINTERID_WPARAM(m.wParam),&p)&&p.pointerType==PT_MOUSE){
   if(omitOwnedUp&&p.ButtonChangeType==POINTER_CHANGE_FIRSTBUTTON_UP)omit=true;
   if(omitReleaseState&&p.ButtonChangeType==POINTER_CHANGE_NONE&&!(p.pointerFlags&POINTER_FLAG_FIRSTBUTTON)){omit=true;++omittedReleaseStates;}
 }}
 }if(omit){++omittedUps;continue;}TranslateMessage(&m);DispatchMessageW(&m);}drainIndependent(c);MsgWaitForMultipleObjectsEx(0,nullptr,2,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}while(GetTickCount64()<end);}
static void guard(POINT at,Carrier* monitor=nullptr){
 POINT p{};GetPhysicalCursorPos(&p);if(cursorKnown&&p.x!=expectedCursor.x||cursorKnown&&p.y!=expectedCursor.y)throw std::runtime_error("Human cursor movement detected; test stopped");
 RECT r{};GetWindowRect(receiver,&r);need(PtInRect(&r,at),"Injection outside owned receiver refused");
 const HWND target=GetAncestor(WindowFromPhysicalPoint(at),GA_ROOT);need(target==receiver||(monitor&&monitor->current&&target==monitor->current->inputWindow),"Owned receiver is not the mouse target; injection refused");
}
static void click(Carrier* c,POINT p){guard(p,c);INPUT a[3]{};for(auto& x:a){x.type=INPUT_MOUSE;x.mi.dwExtraInfo=0x594D4352;}
 const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN),vw=GetSystemMetrics(SM_CXVIRTUALSCREEN),vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 a[0].mi.dx=(LONG)((p.x-vx)*65535LL/(std::max)(1,vw-1));a[0].mi.dy=(LONG)((p.y-vy)*65535LL/(std::max)(1,vh-1));a[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;a[1].mi.dwFlags=MOUSEEVENTF_LEFTDOWN;a[2].mi.dwFlags=MOUSEEVENTF_LEFTUP;
 const auto sent=SendInput(3,a,sizeof(INPUT));injected+=sent;if(sent>=2)markMouse(sent<3);need(sent==3,"Owned mouse input failed");pump(c,70);const bool cursorRead=GetPhysicalCursorPos(&expectedCursor)!=FALSE;lastCursorDelivery={{"requestedX",p.x},{"requestedY",p.y},{"readSucceeded",cursorRead},{"actualX",expectedCursor.x},{"actualY",expectedCursor.y},{"absoluteX",a[0].mi.dx},{"absoluteY",a[0].mi.dy},{"virtualLeft",vx},{"virtualTop",vy},{"virtualWidth",vw},{"virtualHeight",vh},{"inputDesktop",(UINT_PTR)GetThreadDesktop(GetCurrentThreadId())}};need(cursorRead&&std::abs(expectedCursor.x-p.x)<=2&&std::abs(expectedCursor.y-p.y)<=2,"Cursor moved away from owned input point; test stopped");cursorKnown=true;
}
static void mouseFrame(Carrier* c,POINT p,DWORD flags){guard(p,c);INPUT a[2]{};for(auto& x:a){x.type=INPUT_MOUSE;x.mi.dwExtraInfo=0x594D4352;}
 const int vx=GetSystemMetrics(SM_XVIRTUALSCREEN),vy=GetSystemMetrics(SM_YVIRTUALSCREEN),vw=GetSystemMetrics(SM_CXVIRTUALSCREEN),vh=GetSystemMetrics(SM_CYVIRTUALSCREEN);
 a[0].mi.dx=(LONG)((p.x-vx)*65535LL/(std::max)(1,vw-1));a[0].mi.dy=(LONG)((p.y-vy)*65535LL/(std::max)(1,vh-1));a[0].mi.dwFlags=MOUSEEVENTF_MOVE|MOUSEEVENTF_ABSOLUTE|MOUSEEVENTF_VIRTUALDESK;a[1].mi.dwFlags=flags;
 const UINT count=flags?2:1;const auto sent=SendInput(count,a,sizeof(INPUT));injected+=sent;if(sent==count){if(flags&MOUSEEVENTF_LEFTDOWN)markMouse(true);if(flags&MOUSEEVENTF_LEFTUP)markMouse(false);}need(sent==count,"Owned monitor mouse frame failed");
 pump(c,40);GetPhysicalCursorPos(&expectedCursor);need(std::abs(expectedCursor.x-p.x)<=2&&std::abs(expectedCursor.y-p.y)<=2,"Cursor left owned mouse frame");cursorKnown=true;
}
static void tap(Carrier* c,POINT p,int radius=2){guard(p,c);RECT own{};GetWindowRect(receiver,&own);POINT low{p.x-radius,p.y-radius},high{p.x+radius-1,p.y+radius-1};need(PtInRect(&own,low)&&PtInRect(&own,high),"Touch ellipse outside owned receiver refused");POINTER_TOUCH_INFO a{};a.pointerInfo.pointerType=PT_TOUCH;a.pointerInfo.pointerId=0;a.pointerInfo.ptPixelLocation=p;a.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;a.rcContact={p.x-radius,p.y-radius,p.x+radius,p.y+radius};a.orientation=90;a.pressure=512;
 a.pointerInfo.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;need(InjectTouchInput(1,&a)!=FALSE,"Owned touch down failed");++injected;touchHeld=true;lastTouch=p;pump(c,20);
 a.pointerInfo.pointerFlags=POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;need(InjectTouchInput(1,&a)!=FALSE,"Owned touch update failed");++injected;pump(c,20);
 a.pointerInfo.pointerFlags=POINTER_FLAG_UP;need(InjectTouchInput(1,&a)!=FALSE,"Owned touch up failed");++injected;touchHeld=false;pump(c,70);GetPhysicalCursorPos(&expectedCursor);cursorKnown=true;
}
static void touchFrame(Carrier* c,POINT p,POINTER_FLAGS flags){guard(p,c);POINTER_TOUCH_INFO a{};a.pointerInfo.pointerType=PT_TOUCH;a.pointerInfo.pointerId=0;a.pointerInfo.ptPixelLocation=p;a.touchMask=TOUCH_MASK_CONTACTAREA|TOUCH_MASK_ORIENTATION|TOUCH_MASK_PRESSURE;a.rcContact={p.x-2,p.y-2,p.x+2,p.y+2};a.orientation=90;a.pressure=512;a.pointerInfo.pointerFlags=flags;need(InjectTouchInput(1,&a),"Monitor-owned touch frame failed");++injected;touchHeld=(flags&POINTER_FLAG_UP)==0;lastTouch=p;pump(c,25);GetPhysicalCursorPos(&expectedCursor);cursorKnown=true;}
static int worker(const std::filesystem::path& out,const std::wstring& runtime,const std::filesystem::path& web){
 Json result={{"ok",false},{"cases",Json::array()},{"actualWebView2",true},{"actualProductCarrier",true},{"productVue",false},{"installedYmccOperated",false},{"scope","Real production Carrier/WebView2 and OS input into owned lower HWND; no installed app changes."}};bool good=true,com=false;inputMarker=out/L"injected-mouse-held.txt";std::string error;HANDLE browser=nullptr,receiverMapping=nullptr,receiverProcess=nullptr;HWND original=GetForegroundWindow();ComPtr<ICoreWebView2Environment> environment;std::unique_ptr<Carrier> carrier;
 auto check=[&](const char* name,bool pass){result["cases"].push_back({{"name",name},{"ok",pass}});good&=pass;};
 const bool browserExitFault=std::filesystem::exists(out/L"browser-exit-fault.request");result["browserExitFaultRequested"]=browserExitFault;
 try{GetPhysicalCursorPos(&initialCursor);need(!(GetAsyncKeyState(VK_LBUTTON)&0x8000)&&!(GetAsyncKeyState(VK_RBUTTON)&0x8000),"Mouse button held; refuse test");
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);EnableMouseInPointer(TRUE);winrt::init_apartment(winrt::apartment_type::single_threaded);com=true;
 WNDCLASSW c{};c.lpfnWndProc=receive;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"YMCC_RoutingOwnedReceiver";RegisterClassW(&c);MONITORINFO mi{sizeof mi};GetMonitorInfoW(MonitorFromPoint(initialCursor,MONITOR_DEFAULTTOPRIMARY),&mi);
 const int left=mi.rcWork.right-640,top=mi.rcWork.bottom-400;const auto mapName=L"Local\\YMCC_RoutingReceiver_"+std::to_wstring(GetCurrentProcessId());receiverMapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(SharedReceiver),mapName.c_str());need(receiverMapping,"Receiver mapping unavailable");sharedReceiver=(SharedReceiver*)MapViewOfFile(receiverMapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedReceiver));need(sharedReceiver,"Receiver mapping view unavailable");*sharedReceiver=SharedReceiver{};
 wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);std::wstring childCommand=L"\""+std::wstring(self)+L"\" --receiver \""+mapName+L"\" "+std::to_wstring(left)+L" "+std::to_wstring(top);STARTUPINFOW receiverStartup{sizeof receiverStartup};PROCESS_INFORMATION receiverInfo{};
 need(CreateProcessW(self,childCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&receiverStartup,&receiverInfo),"Receiver process unavailable");receiverProcess=receiverInfo.hProcess;CloseHandle(receiverInfo.hThread);
 const auto receiverEnd=GetTickCount64()+3000;while(!sharedReceiver->ready&&GetTickCount64()<receiverEnd)pump(nullptr,10);receiver=(HWND)(UINT_PTR)sharedReceiver->hwnd;need(receiver&&IsWindow(receiver),"Foreign receiver HWND unavailable");result["receiverPid"]=sharedReceiver->pid;result["monitorPid"]=GetCurrentProcessId();need(sharedReceiver->pid!=GetCurrentProcessId(),"Receiver must be another process");
 const POINT target{left+470,top+240};need(InitializeTouchInjection(1,TOUCH_FEEDBACK_NONE)!=FALSE,"Touch injection unavailable");
 auto probe=[&](const char* label,Carrier* current){const auto md=sharedReceiver->mouseDown,mu=sharedReceiver->mouseUp,td=sharedReceiver->touchDown,tu=sharedReceiver->touchUp;click(current,target);result["mouseBeforeTouch"]=Json{{"label",label},{"down",sharedReceiver->mouseDown-md},{"up",sharedReceiver->mouseUp-mu}};tap(current,target);result["cases"].push_back({{"name",label},{"ok",sharedReceiver->mouseDown==md+1&&sharedReceiver->mouseUp==mu+1&&sharedReceiver->touchDown==td+1&&sharedReceiver->touchUp==tu+1},{"mouseDown",sharedReceiver->mouseDown-md},{"mouseUp",sharedReceiver->mouseUp-mu},{"touchDown",sharedReceiver->touchDown-td},{"touchUp",sharedReceiver->touchUp-tu}});good&=sharedReceiver->mouseDown==md+1&&sharedReceiver->mouseUp==mu+1&&sharedReceiver->touchDown==td+1&&sharedReceiver->touchUp==tu+1;};
 probe("baseline lower HWND receives actual mouse and touch clicks",nullptr);
 auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();options->put_AdditionalBrowserArguments(L"--disable-background-networking --disable-component-update --disable-sync --no-first-run --host-resolver-rules=MAP * ~NOTFOUND");options->put_AllowSingleSignOnUsingOSPrimaryAccount(FALSE);
 HRESULT envResult=E_PENDING;need(SUCCEEDED(CreateCoreWebView2EnvironmentWithOptions(runtime.c_str(),(out/L"profile").c_str(),options.Get(),Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&](HRESULT hr,ICoreWebView2Environment* env)->HRESULT{envResult=hr;environment=env;return S_OK;}).Get())),"Environment request failed");
 const auto envEnd=GetTickCount64()+12000;while(envResult==E_PENDING&&GetTickCount64()<envEnd)pump(nullptr,10);need(SUCCEEDED(envResult)&&environment,"Actual environment unavailable");
 screeninput::Runtime inputRuntime;carrier=std::make_unique<Carrier>();Config config;config.notify=[&](const std::string& name,const Json& value){if(!result.contains("monitorEvents"))result["monitorEvents"]=Json::array();result["monitorEvents"].push_back({{"name",name},{"value",value}});};config.anchor=receiver;config.inputRuntime=&inputRuntime;config.serve=[&](ICoreWebView2* view){ComPtr<ICoreWebView2_3> v;return SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&v)))&&SUCCEEDED(v->SetVirtualHostNameToFolderMapping(L"app.localhost",web.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));};carrier->initialize(environment.Get(),config);
 auto own=carrier->create(0,0,"actual-monitor-routing-00000001",true);const auto readyEnd=GetTickCount64()+12000;while(carrier->current&&!carrier->current->ready&&GetTickCount64()<readyEnd)pump(carrier.get(),10);need(carrier->current&&carrier->current->ready,"Actual monitor page handshake failed");auto s=carrier->current;
 UINT32 browserPid=0;s->view->get_BrowserProcessId(&browserPid);browser=OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION|(browserExitFault?PROCESS_TERMINATE:0),FALSE,browserPid);result["browserPid"]=browserPid;
 const double width=mi.rcWork.right-mi.rcWork.left,height=mi.rcWork.bottom-mi.rcWork.top;s->drag.position={(left+20-mi.rcWork.left)/width,(top+20-mi.rcWork.top)/height,true,true,2};
 if(std::filesystem::exists(web/L"frame-fixture.json")){
   Json frame;std::ifstream(web/L"frame-fixture.json")>>frame;frame["type"]="desktop-hud";frame["protocol"]=1;frame["hudId"]=s->id;frame["nonce"]=s->nonce;frame["seq"]=1;frame["show"]=true;frame["expiresAt"]=wallNow()+7000;
   auto& appearance=frame["preferences"]["ymcc"];appearance["dragX"]=s->drag.position.x;appearance["dragY"]=s->drag.position.y;appearance["dragLocked"]=true;appearance["dragRevision"]=2;
   need(carrier->frame(s->id,s->nonce,frame),"Actual renderer initial frame refused");const auto layoutEnd=GetTickCount64()+2500;while(!s->layout&&GetTickCount64()<layoutEnd)pump(carrier.get(),10);
   need(s->layout,"Actual Vue renderer did not confirm panel geometry");result["productVue"]=true;result["actualPanelGeometry"]={{"width",s->width},{"height",s->height},{"screenWidth",width},{"screenHeight",height}};RECT ordinaryDisplay{};GetWindowRect(s->hwnd,&ordinaryDisplay);check("mouse status without crosshair keeps the native display panel-sized",ordinaryDisplay.right-ordinaryDisplay.left==s->width&&ordinaryDisplay.bottom-ordinaryDisplay.top==s->height);
   check("actual Vue renderer reports a content-sized panel inside the foreign receiver",s->width>0&&s->width<540&&s->height>0&&s->height<300);
 }else{s->width=150;s->height=90;s->seq=1;s->expires=wallNow()+60000;s->armed=s->layout=true;s->crosshair=true;}
 carrier->refresh(s);pump(carrier.get(),80);check("real full-screen display has no drag source while explicitly locked",IsWindowVisible(s->hwnd)&&!s->inputWindow);
 probe("full-screen real display alone preserves lower mouse and touch clicks",carrier.get());
 s->drag.position.locked=false;++s->drag.position.revision;carrier->refresh(s);pump(carrier.get(),80);check("unlocked monitor creates only a bounded input window",s->inputWindow&&IsWindowVisible(s->inputWindow));
 probe("full-screen real display plus drag source preserves outside clicks",carrier.get());
 const auto edgeDown=sharedReceiver->touchDown,edgeUp=sharedReceiver->touchUp;
 POINT outsideEdge{s->panel.right+4,s->panel.top+(s->panel.bottom-s->panel.top)/2};tap(carrier.get(),outsideEdge,24);
 check("wide touch ellipse with its actual point outside the HUD reaches the lower process",sharedReceiver->touchDown==edgeDown+1&&sharedReceiver->touchUp==edgeUp+1&&!s->drag.contact());
 const int originalPanelWidth=s->width;const HWND manipulatedInput=s->inputWindow;
 inputPlacementOriginal=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(manipulatedInput,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(alteredInputPlacement)));need(inputPlacementOriginal,"Owned input placement observer unavailable");
 enlargeNextInputPlacement=true;s->width=originalPanelWidth+1;carrier->refresh(s);pump(carrier.get(),30);
 POINT outsideEnlarged{s->panel.right+60,s->panel.top+(s->panel.bottom-s->panel.top)/2};
 const auto placementMd=sharedReceiver->mouseDown,placementMu=sharedReceiver->mouseUp,placementTd=sharedReceiver->touchDown,placementTu=sharedReceiver->touchUp;
 click(carrier.get(),outsideEnlarged);tap(carrier.get(),outsideEnlarged);
 check("altered input placement cannot swallow actual clicks outside the visible panel",sharedReceiver->mouseDown==placementMd+1&&sharedReceiver->mouseUp==placementMu+1&&sharedReceiver->touchDown==placementTd+1&&sharedReceiver->touchUp==placementTu+1);
 bool badGeometryRecorded=false;if(result.contains("monitorEvents"))for(const auto& e:result["monitorEvents"])if(e.value("name",std::string{})=="desktopHud.inputUnavailable"&&e["value"].contains("inputWithdrawal")){
   const auto& w=e["value"]["inputWithdrawal"];badGeometryRecorded=w["inputRect"]["width"]!=w["panelRect"]["width"]&&w.contains("cursorOutsidePanel");}
 check("real geometry failure retains its offending bounds before recovery",badGeometryRecorded);
 result["alteredPlacementOutsideClicks"]={{"mouseDown",sharedReceiver->mouseDown-placementMd},{"mouseUp",sharedReceiver->mouseUp-placementMu},{"touchDown",sharedReceiver->touchDown-placementTd},{"touchUp",sharedReceiver->touchUp-placementTu},{"inputError",s->inputError}};
 if(IsWindow(manipulatedInput))SetWindowLongPtrW(manipulatedInput,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(inputPlacementOriginal));enlargeNextInputPlacement=false;inputPlacementOriginal=nullptr;
 s->width=originalPanelWidth;carrier->refresh(s);pump(carrier.get(),40);check("ordinary update restores dragging after input placement rejection",s->inputWindow&&s->input.active()&&IsWindowVisible(s->inputWindow));
 const HWND externalInput=s->inputWindow;const auto beforeExternal=s->panel;RECT externalRect{};
 SetWindowPos(externalInput,nullptr,beforeExternal.left,beforeExternal.top,s->width+180,s->height+80,SWP_NOZORDER|SWP_NOACTIVATE);
 GetWindowRect(externalInput,&externalRect);
 check("real input mutation stays bounded without a data refresh",EqualRect(&externalRect,&beforeExternal));
 POINT externalPoint{beforeExternal.right+50,beforeExternal.top+35};
 const auto externalMd=sharedReceiver->mouseDown,externalMu=sharedReceiver->mouseUp,externalTd=sharedReceiver->touchDown,externalTu=sharedReceiver->touchUp;
 click(carrier.get(),externalPoint);tap(carrier.get(),externalPoint);
 check("external window mutation preserves actual outside mouse and touch delivery",sharedReceiver->mouseDown==externalMd+1&&sharedReceiver->mouseUp==externalMu+1&&sharedReceiver->touchDown==externalTd+1&&sharedReceiver->touchUp==externalTu+1);
 result["externalMutationOutsideClicks"]={{"mouseDown",sharedReceiver->mouseDown-externalMd},{"mouseUp",sharedReceiver->mouseUp-externalMu},{"touchDown",sharedReceiver->touchDown-externalTd},{"touchUp",sharedReceiver->touchUp-externalTu}};
 SetWindowPos(externalInput,nullptr,beforeExternal.left,beforeExternal.top,s->width+180,s->height+80,SWP_NOSENDCHANGING|SWP_NOZORDER|SWP_NOACTIVATE);
 check("real input bypass withdraws the window synchronously",!s->inputWindow&&!IsWindow(externalInput)&&s->input.terminalReleased());
 const auto bypassMd=sharedReceiver->mouseDown,bypassMu=sharedReceiver->mouseUp,bypassTd=sharedReceiver->touchDown,bypassTu=sharedReceiver->touchUp;
 click(carrier.get(),externalPoint);tap(carrier.get(),externalPoint);
 check("input bypass leaves the lower process immediately clickable",sharedReceiver->mouseDown==bypassMd+1&&sharedReceiver->mouseUp==bypassMu+1&&sharedReceiver->touchDown==bypassTd+1&&sharedReceiver->touchUp==bypassTu+1);
 carrier->refresh(s);pump(carrier.get(),30);
 check("bounded input recovers after the rejected external mutation",s->inputWindow&&s->input.active()&&IsWindowVisible(s->inputWindow));
 const HWND beforeMoveInput=s->inputWindow;const auto beforeDisplayMove=s->panel;
 SetWindowPos(s->hwnd,nullptr,beforeDisplayMove.left+240,beforeDisplayMove.top+100,s->width,s->height,SWP_NOZORDER|SWP_NOACTIVATE);
 check("independent real display move immediately withdraws the old input region",!IsWindowVisible(beforeMoveInput)&&!s->drag.contact());
 POINT oldPanelPoint{beforeDisplayMove.left+30,beforeDisplayMove.top+25};
 const auto movedMd=sharedReceiver->mouseDown,movedMu=sharedReceiver->mouseUp,movedTd=sharedReceiver->touchDown,movedTu=sharedReceiver->touchUp;
 click(carrier.get(),oldPanelPoint);tap(carrier.get(),oldPanelPoint);
 check("vacated display area delivers actual mouse and touch to the lower process",sharedReceiver->mouseDown==movedMd+1&&sharedReceiver->mouseUp==movedMu+1&&sharedReceiver->touchDown==movedTd+1&&sharedReceiver->touchUp==movedTu+1);
 result["vacatedDisplayAreaClicks"]={{"mouseDown",sharedReceiver->mouseDown-movedMd},{"mouseUp",sharedReceiver->mouseUp-movedMu},{"touchDown",sharedReceiver->touchDown-movedTd},{"touchUp",sharedReceiver->touchUp-movedTu}};
 carrier->refresh(s);pump(carrier.get(),30);RECT alignedDisplay{},alignedInput{};GetWindowRect(s->hwnd,&alignedDisplay);if(s->inputWindow)GetWindowRect(s->inputWindow,&alignedInput);
 check("real display movement recovers only an aligned input pair",s->inputWindow&&IsWindowVisible(s->inputWindow)&&EqualRect(&alignedDisplay,&s->panel)&&EqualRect(&alignedInput,&s->panel));
 POINT panelTouch{s->panel.left+30,s->panel.top+25};tap(carrier.get(),panelTouch);pump(carrier.get(),30);
 check("real touch tap releases the monitor contact",!s->drag.contact());
 result["mouseCaptureAfterTouch"]=(UINT_PTR)GetCapture();
 probe("after a real monitor touch tap the foreign process still receives clicks",carrier.get());
 const auto dragStart=s->panel;POINT dragAt{dragStart.left+30,dragStart.top+25};
 touchFrame(carrier.get(),dragAt,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
 result["touchContactImmediatelyAfterDown"]=s->drag.contact();
 // Windows touch targeting may defer the initial DOWN until a stationary
 // contact UPDATE. Establish admission before measuring any displacement.
 touchFrame(carrier.get(),dragAt,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
 check("actual stationary touch contact enters the monitor drag consumer",s->drag.contact());
 for(int i=1;i<=3;++i)touchFrame(carrier.get(),{dragAt.x+i*8,dragAt.y+i*4},POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
 touchFrame(carrier.get(),{dragAt.x+24,dragAt.y+12},POINTER_FLAG_UP);
 check("actual touch drag moves and releases the monitor once",std::abs(s->panel.left-dragStart.left-24)<=1&&std::abs(s->panel.top-dragStart.top-12)<=1&&!s->drag.contact());
 probe("after actual monitor dragging the foreign receiver remains clickable",carrier.get());
 // Real injected touch and OS queue, with failure only at the active pointer's
 // metadata API boundary. Drop the secondary reconciliation solely in our
 // own queue so it cannot conceal an unreadable terminal-message leak.
 POINT metadataAt{s->panel.left+30,s->panel.top+25};touchFrame(carrier.get(),metadataAt,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
 result["metadataContactImmediatelyAfterDown"]=s->touchContact&&s->drag.contact();
 touchFrame(carrier.get(),metadataAt,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
 need(s->touchContact&&s->drag.contact(),"Real pointer metadata fault requires an admitted contact");
 failedLivePointerId=s->touchPointerId;const auto metadataBefore=livePointerMetadataFailures;const auto failedContactId=failedLivePointerId;
 omitReleaseState=true;touchFrame(carrier.get(),metadataAt,POINTER_FLAG_UP);omitReleaseState=false;failedLivePointerId=0;
 check("real active-pointer metadata failure reached the production consumer",livePointerMetadataFailures>metadataBefore);
 check("unreadable real touch release clears drag contact and hides input",!s->touchContact&&!s->drag.contact()&&!s->radialContact&&!IsWindowVisible(s->inputWindow)&&GetCapture()!=s->inputWindow);
 result["pointerMetadataFault"]={{"pointerId",failedContactId},{"apiFailures",livePointerMetadataFailures-metadataBefore},{"inputVisibleAfter",IsWindowVisible(s->inputWindow)!=FALSE},{"touchContactAfter",s->touchContact},{"dragContactAfter",s->drag.contact()},{"captureAfter",(UINT_PTR)GetCapture()}};
 probe("after real touch metadata failure the foreign process receives mouse and touch clicks",carrier.get());
 carrier->refresh(s);pump(carrier.get(),30);POINT afterMetadata{s->panel.left+30,s->panel.top+25};
 touchFrame(carrier.get(),afterMetadata,POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
 touchFrame(carrier.get(),afterMetadata,POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
 check("normal refresh admits a new real touch after metadata cancellation",s->touchContact&&s->drag.contact());
 touchFrame(carrier.get(),afterMetadata,POINTER_FLAG_UP);
 check("the recovered real touch ends without capture or drag residue",!s->touchContact&&!s->drag.contact()&&GetCapture()!=s->inputWindow);

 const auto mouseStart=s->panel;POINT mouseAt{mouseStart.left+35,mouseStart.top+25};mouseFrame(carrier.get(),mouseAt,MOUSEEVENTF_LEFTDOWN);
 check("actual mouse down enters the monitor without acquiring desktop capture",s->drag.contact()&&GetCapture()!=s->inputWindow);
 for(int i=1;i<=3;++i)mouseFrame(carrier.get(),{mouseAt.x+i*7,mouseAt.y+i*3},0);
 mouseFrame(carrier.get(),{mouseAt.x+21,mouseAt.y+9},MOUSEEVENTF_LEFTUP);
 check("capture-free real mouse drag moves once and releases",std::abs(s->panel.left-mouseStart.left-21)<=1&&std::abs(s->panel.top-mouseStart.top-9)<=1&&!s->drag.contact()&&GetCapture()!=s->inputWindow);
 probe("after actual mouse dragging the foreign receiver remains clickable",carrier.get());
 POINT lostAt{s->panel.left+30,s->panel.top+25};mouseFrame(carrier.get(),lostAt,MOUSEEVENTF_LEFTDOWN);
 check("a monitor press waiting for remote UP cannot own desktop mouse capture",s->drag.contact()&&GetCapture()!=s->inputWindow);
 // The OS must see the release. Drop delivery only to our owned monitor:
 // retaining the system-wide physical button state would be a different fault.
 omitOwnedUp=true;mouseFrame(carrier.get(),lostAt,MOUSEEVENTF_LEFTUP);omitOwnedUp=false;
 check("fault injection drops an owned UP after Windows releases the real button",omittedUps>0);
 probe("lost monitor mouse UP cannot swallow the next outside click",carrier.get());mouseHeld=false;
 check("leaving the panel retires the interrupted mouse gesture",!s->drag.contact()&&!s->mouseContact);
 // A remote/focus transition can lose both UP delivery and the following
 // reconciliation events. Drop only those messages addressed to our monitor,
 // while Windows still receives the actual physical release and outside click.
 POINT remoteAt{s->panel.left+30,s->panel.top+25};mouseFrame(carrier.get(),remoteAt,MOUSEEVENTF_LEFTDOWN);
 const auto droppedBefore=omittedUps;omitOwnedUp=omitReleaseState=true;mouseFrame(carrier.get(),remoteAt,MOUSEEVENTF_LEFTUP);
 const auto outsideDown=sharedReceiver->mouseDown,outsideUp=sharedReceiver->mouseUp;click(carrier.get(),target);
 check("lost remote release and reconciliation cannot swallow an outside mouse click",sharedReceiver->mouseDown==outsideDown+1&&sharedReceiver->mouseUp==outsideUp+1);
 result["lostRemoteRelease"]={{"skippedUp",omittedUps-droppedBefore},{"skippedReconciliation",omittedReleaseStates},{"lowerDown",sharedReceiver->mouseDown-outsideDown},{"lowerUp",sharedReceiver->mouseUp-outsideUp},{"captureAfter",(UINT_PTR)GetCapture()}};
 check("remote release-state fault actually skipped an owned UP",omittedUps>droppedBefore);omitOwnedUp=omitReleaseState=false;
 carrier->stopInteraction(s);carrier->refresh(s);pump(carrier.get(),40);probe("after interrupted remote delivery the foreign process remains clickable",carrier.get());
 for(unsigned i=0;i<2;++i){carrier->stopInteraction(s);carrier->refresh(s);pump(carrier.get(),40);probe("repeated drag hide and show preserves outside clicks",carrier.get());}
 if(std::filesystem::exists(web/L"notice-output.html")){
   Carrier notices;Config noticeConfig=config;noticeConfig.noticeOnly=true;noticeConfig.notify=[&](const std::string& name,const Json& value){if(!result.contains("noticeEvents"))result["noticeEvents"]=Json::array();result["noticeEvents"].push_back({{"name",name},{"value",value}});};notices.initialize(environment.Get(),noticeConfig);
   const std::string nonce="actual-notice-routing-00000001";auto lease=notices.reserveNotice(nonce);notices.create(0,0,nonce,true,lease["leaseId"].get<unsigned long long>());
   const auto ready=GetTickCount64()+5000;while(notices.current&&!notices.current->ready&&GetTickCount64()<ready)pump(carrier.get(),10);result["noticeStatusAfterWait"]=notices.status();need(notices.current&&notices.current->ready,"Actual notice renderer handshake failed");auto n=notices.current;
   auto now=wallNow();Json feedback={{"schemaVersion",1},{"id","routing-notice"},{"origin","monitor"},{"actionId","monitor.template"},{"state","applied"},{"title","Monitor template B"},{"message","Monitor template B"},{"occurredAtUnixMs",now},{"expiresAtUnixMs",now+4000},{"powerGeneration",1},{"presentation",{{"fullscreen",true},{"leftMonitor",true}}}};
   Json frame={{"type","monitor-notice"},{"protocol",1},{"hudId",n->id},{"nonce",n->nonce},{"seq",1},{"expiresAt",now+4000},{"feedback",feedback},{"appearance",{{"opacity",.8}}},{"occupied",Json::array()}};
   need(notices.noticeFrame(n->id,n->nonce,frame),"Actual notice frame refused");const auto layout=GetTickCount64()+2000;while(!n->layout&&GetTickCount64()<layout)pump(carrier.get(),10);
   check("real Vue notice displays without any interactive input HWND",n->layout&&IsWindowVisible(n->hwnd)&&hudinput::inert(n->hwnd)&&!n->inputWindow);
   probe("visible full-screen notice plus monitor preserves outside clicks",carrier.get());
   notices.noticeHide(n->id,n->nonce,n->seq);check("real notice hide withdraws the actual display",!IsWindowVisible(n->hwnd));probe("hidden notice plus monitor preserves outside clicks",carrier.get());
   const HWND noticeWindow=n->hwnd;notices.close(n->id,n->nonce);check("real notice close destroys its actual native HWND",!IsWindow(noticeWindow));probe("destroyed notice leaves outside clicks available",carrier.get());
 }
 if(browserExitFault){
   need(browser&&GetProcessId(browser)==browserPid&&browserPid!=GetCurrentProcessId()&&carrier->current==s&&!s->closed,"Own browser failure target unavailable");
   const HWND failedDisplay=s->hwnd,failedInput=s->inputWindow;POINT press{s->panel.left+30,s->panel.top+25};mouseFrame(carrier.get(),press,MOUSEEVENTF_LEFTDOWN);
   check("browser failure starts with an admitted monitor contact",s->drag.contact());
   need(TerminateProcess(browser,0x594D4343u)!=FALSE,"Fixture-owned browser termination failed");
   const auto retired=GetTickCount64()+5000;while(carrier->current&&GetTickCount64()<retired)pump(carrier.get(),10);
   mouseFrame(carrier.get(),press,MOUSEEVENTF_LEFTUP);
   check("actual browser exit destroys display and input with no retained gesture",!carrier->current&&!IsWindow(failedDisplay)&&!IsWindow(failedInput)&&!s->drag.contact()&&!s->mouseContact&&!s->touchContact&&GetCapture()!=failedInput);
   probe("unexpected browser exit leaves the foreign process clickable",carrier.get());
   carrier->refresh(s);check("late refresh cannot revive the browser-failed owner",!carrier->current&&!s->hwnd&&!s->inputWindow);
   result["browserFailureRetired"]=!carrier->current;
 }
 carrier->close(s->id,s->nonce);bool withdrawalLogged=false;if(result.contains("monitorEvents"))for(const auto& event:result["monitorEvents"])if(event.value("name",std::string{})=="desktopHud.closed")withdrawalLogged=event["value"].contains("inputWithdrawal")&&event["value"]["inputWithdrawal"].value("inputMode",std::string{})=="native-panel-no-capture-v1"&&event["value"]["inputWithdrawal"].dump().size()<2048;
 bool releasedEvidence=false;if(result.contains("monitorEvents"))for(const auto& event:result["monitorEvents"])if(event.value("name",std::string{})=="desktopHud.closed"&&event["value"].value("id",0)==s->id&&event["value"].value("nonce",std::string{})==s->nonce&&event["value"].contains("inputRelease")){
   const auto& r=event["value"]["inputRelease"];releasedEvidence=r.value("inputDestroyed",false)&&r.value("displayDestroyed",false)&&r.value("surfaceReleased",false)&&r.value("controllerReleased",false)&&r.value("contactsCleared",false)&&!r.value("captureRetained",true);
 }
 check("actual WebView close reports completed input and display release",releasedEvidence);
 check("actual close lifecycle retains bounded input geometry before cleanup",withdrawalLogged);s.reset();carrier.reset();probe("after real monitor close lower clicks remain healthy",nullptr);environment.Reset();
 const auto end=GetTickCount64()+10000;while(browser&&WaitForSingleObject(browser,0)==WAIT_TIMEOUT&&GetTickCount64()<end)pump(nullptr,20);check(browserExitFault?"fixture-terminated browser exit is confirmed":"owned WebView browser exits after close",browser&&WaitForSingleObject(browser,0)==WAIT_OBJECT_0);
 }catch(const std::exception& e){error=e.what();good=false;}catch(const winrt::hresult_error&e){error="WinRT "+std::to_string((long)e.code());good=false;}
 if(touchHeld){POINTER_TOUCH_INFO a{};a.pointerInfo.pointerType=PT_TOUCH;a.pointerInfo.pointerId=0;a.pointerInfo.ptPixelLocation=lastTouch;a.pointerInfo.pointerFlags=POINTER_FLAG_UP;InjectTouchInput(1,&a);touchHeld=false;}
 if(mouseHeld){INPUT up{};up.type=INPUT_MOUSE;up.mi.dwFlags=MOUSEEVENTF_LEFTUP;result["mouseReleaseOnAbort"]=SendInput(1,&up,sizeof(up))==1;markMouse(false);}
 carrier.reset();environment.Reset();if(receiver&&IsWindow(receiver)){DWORD_PTR ignored=0;SendMessageTimeoutW(receiver,WM_CLOSE,0,0,SMTO_ABORTIFHUNG,1000,&ignored);}if(receiverProcess){result["receiverNaturalExit"]=WaitForSingleObject(receiverProcess,2000)==WAIT_OBJECT_0;CloseHandle(receiverProcess);}if(sharedReceiver){UnmapViewOfFile(sharedReceiver);sharedReceiver=nullptr;}if(receiverMapping)CloseHandle(receiverMapping);if(browser)CloseHandle(browser);if(com)winrt::uninit_apartment();POINT cursor{};GetPhysicalCursorPos(&cursor);if(cursorKnown&&cursor.x==expectedCursor.x&&cursor.y==expectedCursor.y)SetCursorPos(initialCursor.x,initialCursor.y);
 if(GetForegroundWindow()!=original&&GetWindowThreadProcessId(GetForegroundWindow(),nullptr)==GetCurrentThreadId()&&IsWindow(original))SetForegroundWindow(original);
 result["ok"]=good;result["error"]=error;result["injectedPackets"]=injected;result["lastCursorDelivery"]=lastCursorDelivery;result["receivedPointerEvents"]=pointers;std::ofstream(out/L"native-result.json")<<result.dump(2);std::cout<<Json{{"ok",good},{"error",error},{"cases",result["cases"].size()}}.dump()<<std::endl;return error.empty()?(good?0:3):4;
}
static int receiverMain(const wchar_t* mapName,int left,int top){
 HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,mapName);if(!mapping)return 5;sharedReceiver=(SharedReceiver*)MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(SharedReceiver));if(!sharedReceiver)return 5;SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);EnableMouseInPointer(TRUE);
 WNDCLASSW c{};c.lpfnWndProc=receive;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"YMCC_RoutingOwnedReceiver";RegisterClassW(&c);HWND h=CreateWindowExW(WS_EX_TOPMOST|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,c.lpszClassName,L"YMCC independent routing receiver",WS_POPUP,left,top,600,360,nullptr,nullptr,c.hInstance,nullptr);if(!h)return 5;ShowWindow(h,SW_SHOWNOACTIVATE);UpdateWindow(h);sharedReceiver->hwnd=(UINT64)(UINT_PTR)h;sharedReceiver->pid=GetCurrentProcessId();InterlockedExchange(&sharedReceiver->ready,1);MSG m;while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}UnmapViewOfFile(sharedReceiver);CloseHandle(mapping);return 0;
}
int wmain(int argc,wchar_t**argv){if(argc!=4&&argc!=5)return 2;if(argc==5&&std::wstring(argv[1])==L"--receiver")return receiverMain(argv[2],_wtoi(argv[3]),_wtoi(argv[4]));if(argc==5&&std::wstring(argv[1])==L"--worker")return worker(argv[2],argv[3],argv[4]);
 wchar_t self[32768]{};GetModuleFileNameW(nullptr,self,32768);auto quoted=[](const std::wstring& x){return L"\""+x+L"\"";};std::wstring command=quoted(self)+L" --worker "+quoted(argv[1])+L" "+quoted(argv[2])+L" "+quoted(argv[3]);
 HANDLE job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limit{};limit.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(job&&SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limit,sizeof limit),"Owned job unavailable");STARTUPINFOW startup{sizeof startup};PROCESS_INFORMATION process{};need(CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process),"Worker unavailable");need(AssignProcessToJobObject(job,process.hProcess),"Owned worker assignment failed");ResumeThread(process.hThread);CloseHandle(process.hThread);const DWORD wait=WaitForSingleObject(process.hProcess,60000);DWORD code=4;if(wait==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&code);else {DWORD markerPid=0;std::ifstream(std::filesystem::path(argv[1])/L"injected-mouse-held.txt")>>markerPid;if(markerPid==process.dwProcessId){INPUT up{};up.type=INPUT_MOUSE;up.mi.dwFlags=MOUSEEVENTF_LEFTUP;SendInput(1,&up,sizeof(up));}TerminateJobObject(job,4);}CloseHandle(process.hProcess);CloseHandle(job);return code;
}
