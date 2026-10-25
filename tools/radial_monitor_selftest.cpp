// Reuse the existing inactive-desktop launcher and native renderer helpers.
// The build inserts test friendship only; all production consumer bodies are unchanged.
#define wmain radialLegacyEntry
#include "radial-test-helpers.cpp"
#undef wmain
namespace ymcc::desktophud {
struct MonitorJoinTestAccess {
 static HWND output(Carrier& c){return c.current?c.current->hwnd:nullptr;}
 static HWND input(Carrier& c){return c.current?c.current->inputWindow:nullptr;}
 static ICoreWebView2* view(Carrier& c){return c.current?c.current->view.Get():nullptr;}
 static RECT panel(Carrier& c){return c.current?c.current->panel:RECT{};}
 static bool ready(Carrier& c){return c.current&&c.current->ready;}
 static bool locked(Carrier& c){return c.current&&c.current->drag.position.locked;}
 static unsigned long long dragRevision(Carrier& c){return c.current?c.current->drag.position.revision:0;}
 static std::string currentTemplate(Carrier& c){return c.current?c.current->activeTemplate:std::string{};}
 static void pointer(Carrier& c,screeninput::Phase phase,unsigned id,float x,float y,LONGLONG age=0,UINT32 type=PT_TOUCH){auto s=c.current;if(!s)return;screeninput::Event e;e.type=type;e.phase=phase;e.id=id;e.x=x;e.y=y;e.cancelled=phase==screeninput::Phase::Cancel;e.timeKnown=true;e.sourceTime=static_cast<DWORD>(GetTickCount64()-age);Carrier::inputEvent(s->inputWindow,e,s.get());}
 static void cancelInput(Carrier& c){if(c.current)c.stopInteraction(c.current);}
 static void refreshInput(Carrier& c){if(c.current)c.refresh(c.current);}
 static void hold(Carrier& c,ULONGLONG age){if(!c.current)return;c.current->radialDownAt=GetTickCount64()-age;SendMessageW(c.current->hwnd,WM_TIMER,Carrier::holdTimer,0);}
};
}
static Json monitorScript(ymcc::desktophud::Carrier& carrier,const std::wstring& script){auto* view=ymcc::desktophud::MonitorJoinTestAccess::view(carrier);ensure(view!=nullptr,"Monitor renderer absent");bool done=false;HRESULT result=E_PENDING;std::wstring value;const auto hr=view->ExecuteScript(script.c_str(),Callback<ICoreWebView2ExecuteScriptCompletedHandler>([&](HRESULT code,LPCWSTR data)->HRESULT{result=code;if(data)value=data;done=true;return S_OK;}).Get());ensure(SUCCEEDED(hr)&&until([&]{return done;},6000)&&SUCCEEDED(result),"Monitor renderer script failed");return Json::parse(ymcc::desktophud::utf8(value));}
static void monitorCapture(ymcc::desktophud::Carrier& carrier,const std::filesystem::path& file){
 auto* view=ymcc::desktophud::MonitorJoinTestAccess::view(carrier);ensure(view!=nullptr,"Monitor capture view absent");ComPtr<IStream> stream;ensure(SUCCEEDED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)),"Monitor capture stream");bool done=false;HRESULT code=E_PENDING;
 ensure(SUCCEEDED(view->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream.Get(),Callback<ICoreWebView2CapturePreviewCompletedHandler>([&](HRESULT result)->HRESULT{code=result;done=true;return S_OK;}).Get()))&&until([&]{return done;},10000)&&SUCCEEDED(code),"Monitor capture failed");
 STATSTG size{};ensure(SUCCEEDED(stream->Stat(&size,STATFLAG_NONAME)),"Monitor capture size");HGLOBAL memory=nullptr;GetHGlobalFromStream(stream.Get(),&memory);auto* data=GlobalLock(memory);std::ofstream output(file,std::ios::binary);output.write(static_cast<const char*>(data),static_cast<std::streamsize>(size.cbSize.QuadPart));GlobalUnlock(memory);ensure(output.good(),"Monitor capture write");
}
static int monitorWorker(const std::filesystem::path& web,const std::filesystem::path& out,const std::filesystem::path& fixture){
 using Carrier=ymcc::desktophud::Carrier;using Access=ymcc::desktophud::MonitorJoinTestAccess;using Phase=ymcc::screeninput::Phase;
 Json report={{"pass",false},{"cases",Json::array()},{"privateDesktop",true},{"physicalTouch",false},{"systemInputInjected",false},{"normalYmccStarted",false},{"hardwareWrites",0},{"configurationWrites",0},{"scope","Actual monitor Carrier, HUD renderer, native hold timer, wheel Overlay and return; contact packets enter only the owned consumer"}};
 auto check=[&](const char* name,bool value){report["cases"].push_back({{"name",name},{"pass",value}});ensure(value,name);};
 Carrier carrier;Overlay overlay;ymcc::screeninput::Runtime input;ComPtr<ICoreWebView2Environment> environment;HWND anchor=nullptr;bool com=false,holdEnabled=true;unsigned summons=0,hapticStops=0;bool heldHaptic=false,monitorVisibleAtRequest=false;std::string callbackError;Json wheelReceipt;std::vector<Json> intents;
 try{
  check("owned worker STA",SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)));com=true;check("same controlled-input runtime",input.ensure());std::ifstream file(fixture);Json frame;file>>frame;
  WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=anchorProc;wc.lpszClassName=L"YMCC_MonitorRadial_OwnedAnchor";check("owned anchor class",RegisterClassW(&wc)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS);
  anchor=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,wc.lpszClassName,L"Owned monitor anchor",WS_POPUP,20,20,300,400,nullptr,nullptr,wc.hInstance,nullptr);check("owned anchor window",anchor!=nullptr);ShowWindow(anchor,SW_SHOWNOACTIVATE);
  bool envReady=false;HRESULT envCode=E_PENDING;const auto profile=out/L"monitor-webview-profile";const auto hr=CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),nullptr,Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&](HRESULT code,ICoreWebView2Environment* env)->HRESULT{envCode=code;environment=env;envReady=true;return S_OK;}).Get());
  check("actual WebView environment",SUCCEEDED(hr)&&until([&]{return envReady;},20000)&&SUCCEEDED(envCode)&&environment);
  auto serve=[&](ICoreWebView2* view){ComPtr<ICoreWebView2_3> map;return SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&map)))&&SUCCEEDED(map->SetVirtualHostNameToFolderMapping(L"app.localhost",web.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));};
  overlay.initialize(environment.Get(),{serve,[&](const std::string& event,const Json& value){if(event=="radial.intent")intents.push_back(value);},[](bool){},[](bool,bool){},&input,anchor,[&](bool value){carrier.suspendForRadial(value);}});
  carrier.initialize(environment.Get(),{serve,[](const std::string&,const Json&){},anchor,false,&input,[&](bool contact,bool){heldHaptic=contact;if(!contact)++hapticStops;},[&](int x,int y){++summons;monitorVisibleAtRequest=IsWindowVisible(Access::output(carrier))!=FALSE;try{wheelReceipt=overlay.create("owned-monitor-wheel-session-"+std::to_string(summons),frame["wheel"],"monitor",x,y);}catch(const std::exception& e){callbackError=e.what();}},[&]{return holdEnabled;},[]{return 650u;}});
  const std::string nonce="owned-monitor-carrier-session";const auto receipt=carrier.create(0,0,nonce,true);const int id=receipt["id"];
  check("actual monitor ready IPC",until([&]{return Access::ready(carrier);},20000));unsigned long long seq=0;
  auto refresh=[&]{auto& payload=frame["monitor"];payload["hudId"]=id;payload["nonce"]=nonce;payload["seq"]=++seq;payload["expiresAt"]=ymcc::desktophud::wallNow()+6000;check("monitor frame admitted",carrier.frame(id,nonce,payload));};
  refresh();check("real HUD and controlled input become visible",until([&]{return carrier.status().value("visible",false)&&IsWindowVisible(Access::input(carrier));},10000));
  // Cancellation must reach the actual WebView before the hold animation could expire itself.
  auto noHoldVisual=[&]{return monitorScript(carrier,L"!document.getElementById('hud').classList.contains('radial-holding') && document.documentElement.dataset.hudDragging !== 'true'")==true;};
  Access::pointer(carrier,ymcc::screeninput::Phase::Down,701,30,25);
  check("real monitor starts hold feedback",until([&]{return monitorScript(carrier,L"document.getElementById('hud').classList.contains('radial-holding')")==true;},200));
  Access::cancelInput(carrier);check("native canceled input clears real renderer immediately",until(noHoldVisual,200));Access::refreshInput(carrier);
  Access::pointer(carrier,ymcc::screeninput::Phase::Down,702,30,25);Access::pointer(carrier,ymcc::screeninput::Phase::Move,702,65,45);
  check("actual moving monitor has drag glow",until([&]{return monitorScript(carrier,L"document.documentElement.dataset.hudDragging === 'true'")==true;},200));
  Access::pointer(carrier,ymcc::screeninput::Phase::Up,702,65,45);check("native release clears actual drag feedback",until(noHoldVisual,200));
  Access::pointer(carrier,ymcc::screeninput::Phase::Down,704,30,25);Access::pointer(carrier,ymcc::screeninput::Phase::Move,704,65,45);
  check("future-stamped release starts from real active glow",until([&]{return monitorScript(carrier,L"document.documentElement.dataset.hudDragging === 'true'")==true;},200));
  Access::pointer(carrier,ymcc::screeninput::Phase::Up,704,65,45,-1000);
  check("future-stamped native release clears real WebView glow",until(noHoldVisual,200));
  check("future-stamped release saves a JS-safe placement",Access::dragRevision(carrier)<=9007199254740991ULL);
  Access::pointer(carrier,ymcc::screeninput::Phase::Down,703,30,25);ShowWindow(Access::output(carrier),SW_HIDE);check("native hide revokes hold feedback",until(noHoldVisual,200));Access::refreshInput(carrier);
  if(frame.value("feedbackOnly",false)){
   check("idle renderer has no drag glow",monitorScript(carrier,L"getComputedStyle(document.querySelector('[data-monitor-hud]')).boxShadow === 'none' && getComputedStyle(document.getElementById('hud')).filter === 'none'")==true);
   check("cancellation released existing haptic owner",!heldHaptic&&hapticStops>0);monitorCapture(carrier,out/L"monitor-feedback-idle.png");report["pass"]=true;report["scope"]="Actual native monitor Carrier + WebView2 feedback cleanup only; radial product gate is unchanged";
  }else{
  auto original=Access::panel(carrier);monitorCapture(carrier,out/L"monitor-before.png");const auto originalTemplate=Access::currentTemplate(carrier);const bool originalLock=Access::locked(carrier);check("actual monitor has content",original.right>original.left&&original.bottom>original.top);
  auto pointer=[&](Phase phase,unsigned finger,ULONGLONG age=0,float x=12,float y=12){Access::pointer(carrier,phase,finger,x,y,age);};
  pointer(Phase::Down,1);check("touch-down starts real renderer hold animation",until([&]{return monitorScript(carrier,L"document.getElementById('hud').classList.contains('radial-holding')")==true;}));pointer(Phase::Up,1);check("short contact does not summon",summons==0);check("short contact ends hold indicator",until([&]{return monitorScript(carrier,L"document.getElementById('hud').classList.contains('radial-holding')")==false;}));
  pointer(Phase::Down,2);pointer(Phase::Move,2,0,35,12);Access::hold(carrier,700);pointer(Phase::Up,2,0,35,12);check("moving contact cannot turn into long-hold summon",summons==0);const auto afterMove=Access::panel(carrier);check("moving contact retains existing monitor drag",!EqualRect(&original,&afterMove));original=afterMove;
  pointer(Phase::Down,3);pointer(Phase::Cancel,3);Access::hold(carrier,700);check("canceled contact never summons",summons==0&&!heldHaptic);
  pointer(Phase::Down,4);Access::hold(carrier,100);check("early native timer reschedules instead of summoning",summons==0);
  Access::hold(carrier,700);check("native timer arms without summoning before release",summons==0);pointer(Phase::Up,4);check("matching release publishes exactly one summon",summons==1&&callbackError.empty()&&overlay.active());check("monitor remains visible until wheel renderer ready",monitorVisibleAtRequest);
  check("actual wheel ready hides original monitor",until([&]{return OverlayTestAccess::ready(overlay)&&!IsWindowVisible(Access::output(carrier))&&!IsWindowVisible(Access::input(carrier));},20000));check("monitor's haptic owner released at handoff",!heldHaptic&&hapticStops>0);
  check("wheel opening animation reaches full scale",until([&]{return evaluate(overlay,L"document.querySelector('.radial-wheel').getBoundingClientRect().width >= innerWidth-1")==true;}));
  capture(overlay,out/L"monitor-open-wheel.png");OverlayTestAccess::pointer(overlay,Phase::Up,248,138,4);check("originating contact release cannot execute a wheel action",intents.empty());
  check("actual monitor-sourced wheel has eight primary sectors",evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==8);
  OverlayTestAccess::pointer(overlay,Phase::Down,248,138,10);OverlayTestAccess::pointer(overlay,Phase::Up,248,138,10);
  check("native touch opens TDP draft",until([&]{return evaluate(overlay,L"!!document.querySelector('[data-radial-dial-thumb]')")==true;}));
  OverlayTestAccess::pointer(overlay,Phase::Down,248,528,11);OverlayTestAccess::pointer(overlay,Phase::Move,294,528,11);OverlayTestAccess::pointer(overlay,Phase::Up,294,528,11);
  check("native footer drag cannot submit TDP",stable([&]{return intents.empty();}));
  OverlayTestAccess::pointer(overlay,Phase::Down,248,528,12);OverlayTestAccess::pointer(overlay,Phase::Cancel,248,528,12,true);OverlayTestAccess::pointer(overlay,Phase::Up,248,528,12);
  check("canceled native footer cannot revive on late release",stable([&]{return intents.empty();}));
  OverlayTestAccess::pointer(overlay,Phase::Down,248,38,15);OverlayTestAccess::pointer(overlay,Phase::Down,248,528,16);OverlayTestAccess::pointer(overlay,Phase::Up,248,528,16);OverlayTestAccess::pointer(overlay,Phase::Up,248,38,15);
  check("second native contact cannot confirm first contact dial",stable([&]{return intents.empty();}));
  OverlayTestAccess::pointer(overlay,Phase::Down,248,528,13);OverlayTestAccess::pointer(overlay,Phase::Up,248,528,13);
  check("single native footer tap confirms exactly once",until([&]{return intents.size()==1;})&&stable([&]{return intents.size()==1;}));
  overlay.result(wheelReceipt["id"],wheelReceipt["nonce"],{{"requestId",intents[0]["intent"]["requestId"]},{"ok",true},{"message","owned no-hardware receipt"}});

  const int wheelId=wheelReceipt["id"];const std::string wheelNonce=wheelReceipt["nonce"];check("exact wheel close accepted",overlay.close(wheelId,wheelNonce));
  check("monitor and input return visible",until([&]{return IsWindowVisible(Access::output(carrier))&&IsWindowVisible(Access::input(carrier));}));
  const auto returned=Access::panel(carrier);check("return retains exact monitor panel bounds",EqualRect(&original,&returned));check("return retains template and lock state",originalTemplate==Access::currentTemplate(carrier)&&originalLock==Access::locked(carrier));
  check("real monitor receives return animation",until([&]{return monitorScript(carrier,L"document.getElementById('hud').classList.contains('radial-monitor-return')")==true;}));
  ensure(until([&]{return monitorScript(carrier,L"document.getAnimations().every(a=>a.playState==='finished')")==true;}),"Monitor return animation never completed");monitorCapture(carrier,out/L"monitor-return.png");
  refresh();frame["monitor"]["preferences"]["ymcc"]["dragLocked"]=true;frame["monitor"]["preferences"]["ymcc"]["dragRevision"]=ymcc::desktophud::wallNow();refresh();check("locked appearance reaches native monitor",Access::locked(carrier));
  pointer(Phase::Down,5);Access::hold(carrier,700);check("locked monitor waits for release",summons==1);pointer(Phase::Up,5);check("locked monitor release supports radial hold",summons==2&&callbackError.empty());check("second wheel ready",until([&]{return OverlayTestAccess::ready(overlay);}));overlay.close(wheelReceipt["id"],wheelReceipt["nonce"]);check("locked monitor returns",until([&]{return carrier.status().value("visible",false);}));
  pointer(Phase::Down,6,700);pointer(Phase::Up,6);check("coalesced delayed down and up still summons once",summons==3&&callbackError.empty());check("third wheel ready",until([&]{return OverlayTestAccess::ready(overlay);}));overlay.close(wheelReceipt["id"],wheelReceipt["nonce"]);check("third return keeps original monitor",until([&]{return carrier.status().value("visible",false);}));
  holdEnabled=false;frame["monitor"]["preferences"]["ymcc"]["dragLocked"]=false;frame["monitor"]["preferences"]["ymcc"]["dragRevision"]=Access::dragRevision(carrier)+1;refresh();
  check("explicit settings unlocks after radial return",!Access::locked(carrier)&&until([&]{return IsWindowVisible(Access::input(carrier));}));
  const auto unlockedTemplate=Access::currentTemplate(carrier);const auto unlockedRevision=Access::dragRevision(carrier);
  for(const auto type:{PT_TOUCH,PT_MOUSE,PT_PEN}){
   Access::pointer(carrier,Phase::Down,7,12,12,700,type);Access::hold(carrier,700);Access::pointer(carrier,Phase::Up,7,12,12,0,type);
   check("disabled radial hold and stale timer never lock monitor",!Access::locked(carrier)&&Access::dragRevision(carrier)==unlockedRevision);
   check("disabled radial hold never summons or switches template",summons==3&&Access::currentTemplate(carrier)==unlockedTemplate);
  }
  report["monitorBefore"]={original.left,original.top,original.right,original.bottom};report["monitorAfter"]={returned.left,returned.top,returned.right,returned.bottom};report["pass"]=true;
  }
 }catch(const std::exception& e){report["error"]=e.what();report["carrierStatus"]=carrier.status();report["summons"]=summons;report["callbackError"]=callbackError;}
 overlay.shutdown();carrier.shutdown();environment.Reset();if(anchor)DestroyWindow(anchor);if(com)CoUninitialize();std::ofstream saved(out/L"native-overlay.json");saved<<report.dump(2);std::cout<<Json{{"pass",report["pass"]},{"cases",report["cases"].size()},{"error",report.value("error",std::string{})}}.dump()<<std::endl;return report["pass"].get<bool>()?0:2;
}
int wmain(int argc,wchar_t** argv){SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);if(argc==5&&std::wstring(argv[1])==L"--private-worker")return monitorWorker(argv[2],argv[3],argv[4]);return radialLegacyEntry(argc,argv);}

