#define NOMINMAX
#define YMCC_RADIAL_NATIVE_TEST 1
#include "../native/radial_overlay.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
#include <thread>
namespace ymcc::radial {
struct OverlayTestAccess {
 static HWND output(Overlay& o){return o.current?o.current->window:nullptr;}
 static HWND input(Overlay& o){return o.current?o.current->inputWindow:nullptr;}
 static bool ready(Overlay& o){return o.current&&o.current->ready;}
 static double scale(Overlay& o){return o.current?o.current->scale:1;}
 static ICoreWebView2* view(Overlay& o){return o.current?o.current->view.Get():nullptr;}
 static void pointer(Overlay& o,screeninput::Phase phase,float x,float y,unsigned id,bool cancelled=false){auto s=o.current;if(!s)return;screeninput::Event e;e.phase=phase;e.id=id;e.type=PT_TOUCH;e.x=x*(float)s->scale;e.y=y*(float)s->scale;e.cancelled=cancelled;Overlay::pointer(s->inputWindow,e,s.get());}
 static bool closing(Overlay& o){return o.current&&o.current->closing;}
 static void expire(Overlay& o){auto s=o.current;if(!s)return;s->deadline=GetTickCount64()-1;SendMessageW(s->window,WM_TIMER,Overlay::expireTimer,0);}
};
}
namespace ymcc::desktophud {struct CarrierTestAccess {
 static ICoreWebView2* view(Carrier& c){return c.current?c.current->view.Get():nullptr;}
 static HWND window(Carrier& c){return c.current?c.current->hwnd:nullptr;}
 static HWND input(Carrier& c){return c.current?c.current->inputWindow:nullptr;}
};}
using Json=nlohmann::json;using ymcc::radial::Overlay;using ymcc::radial::OverlayTestAccess;
using Microsoft::WRL::Callback;using Microsoft::WRL::ComPtr;
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message!=WM_QUIT){TranslateMessage(&m);DispatchMessageW(&m);}}}
template<class Fn>static bool until(Fn test,DWORD timeout=12000){const auto end=GetTickCount64()+timeout;do{pump();if(test())return true;MsgWaitForMultipleObjects(0,nullptr,FALSE,10,QS_ALLINPUT);}while(GetTickCount64()<end);return test();}
template<class Fn>static bool stable(Fn test,DWORD timeout=3000){ULONGLONG since=0;return until([&]{if(!test()){since=0;return false;}if(!since)since=GetTickCount64();return GetTickCount64()-since>=120;},timeout);}
static Json evaluateView(ICoreWebView2* view,const std::wstring& script){if(!view)throw std::runtime_error("No live renderer");bool complete=false;HRESULT outcome=E_PENDING;std::wstring value;
 const auto hr=view->ExecuteScript(script.c_str(),Callback<ICoreWebView2ExecuteScriptCompletedHandler>([&](HRESULT result,LPCWSTR json)->HRESULT{outcome=result;if(json)value=json;complete=true;return S_OK;}).Get());
 if(FAILED(hr)||!until([&]{return complete;},6000)||FAILED(outcome))throw std::runtime_error("Owned wheel script failed");return Json::parse(ymcc::desktophud::utf8(value));}
static Json evaluate(Overlay& overlay,const std::wstring& script){return evaluateView(OverlayTestAccess::view(overlay),script);}
static void capture(Overlay& overlay,const std::filesystem::path& file){ComPtr<IStream> stream;if(FAILED(CreateStreamOnHGlobal(nullptr,TRUE,&stream)))throw std::runtime_error("Capture stream unavailable");bool complete=false;HRESULT result=E_PENDING;
 const auto hr=OverlayTestAccess::view(overlay)->CapturePreview(COREWEBVIEW2_CAPTURE_PREVIEW_IMAGE_FORMAT_PNG,stream.Get(),Callback<ICoreWebView2CapturePreviewCompletedHandler>([&](HRESULT code)->HRESULT{result=code;complete=true;return S_OK;}).Get());
 if(FAILED(hr)||!until([&]{return complete;},10000)||FAILED(result))throw std::runtime_error("Owned wheel capture failed");STATSTG info{};if(FAILED(stream->Stat(&info,STATFLAG_NONAME)))throw std::runtime_error("Capture size unavailable");HGLOBAL memory=nullptr;GetHGlobalFromStream(stream.Get(),&memory);auto* bytes=GlobalLock(memory);std::ofstream out(file,std::ios::binary);out.write(static_cast<const char*>(bytes),static_cast<std::streamsize>(info.cbSize.QuadPart));GlobalUnlock(memory);if(!out.good())throw std::runtime_error("Capture write failed");}
static LRESULT CALLBACK anchorProc(HWND h,UINT m,WPARAM w,LPARAM l){if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;if(m==WM_ERASEBKGND)return 1;return DefWindowProcW(h,m,w,l);}
static void ensure(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
static int worker(const std::filesystem::path& web,const std::filesystem::path& out,const std::filesystem::path& fixture){
 Json report={{"pass",false},{"scope","actual native Overlay + WebView2 + built RadialOutput; synthetic admitted-source events"},{"privateDesktop",true},{"physicalController",false},{"physicalTouch",false},{"hardwareWrites",0},{"configurationWrites",0},{"systemInputInjected",false},{"normalYmccStarted",false},{"cases",Json::array()}};
 auto check=[&](const char* name,bool value){report["cases"].push_back({{"name",name},{"pass",value}});ensure(value,name);};
 bool initialized=false;HWND anchor=nullptr,background=nullptr;ComPtr<ICoreWebView2Environment> environment;Overlay overlay;ymcc::screeninput::Runtime input;
 std::vector<Json> intents,events;bool owns=false,visible=false;unsigned feedback=0;std::vector<std::pair<bool,bool>> feedbackTrace;int feedbackStrength=20;HRESULT envError=E_PENDING;std::string failure;
 try{
  check("STA initialization",SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED)));initialized=true;
  check("actual controlled-input runtime initializes",input.ensure());
  std::ifstream read(fixture);Json payload;read>>payload;check("fixture is built from production model",payload.contains("settings")&&payload["settings"]["pages"][0]["items"].size()==8);
  WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=anchorProc;cls.lpszClassName=L"YMCC_RadialNativeTest_Anchor";check("owned test window class",RegisterClassW(&cls)!=0||GetLastError()==ERROR_CLASS_ALREADY_EXISTS);
  MONITORINFO info{sizeof(info)};GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&info);const RECT area=info.rcWork;
  background=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Owned radial test background",WS_POPUP,area.left,area.top,area.right-area.left,area.bottom-area.top,nullptr,nullptr,cls.hInstance,nullptr);ShowWindow(background,SW_SHOWNOACTIVATE);
  anchor=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW,cls.lpszClassName,L"Owned YMCC placement anchor",WS_POPUP,area.left+16,area.top+30,300,400,nullptr,nullptr,cls.hInstance,nullptr);ShowWindow(anchor,SW_SHOWNOACTIVATE);
  check("owned background and anchor",background&&anchor);
  const auto profile=out/L"webview-profile";bool envReady=false;const auto request=CreateCoreWebView2EnvironmentWithOptions(nullptr,profile.c_str(),nullptr,Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&](HRESULT code,ICoreWebView2Environment* value)->HRESULT{envError=code;environment=value;envReady=true;return S_OK;}).Get());
  check("WebView2 environment callback",SUCCEEDED(request)&&until([&]{return envReady;},20000)&&SUCCEEDED(envError)&&environment);
  overlay.initialize(environment.Get(),{
   [&](ICoreWebView2* view){ComPtr<ICoreWebView2_3> map;return SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&map)))&&SUCCEEDED(map->SetVirtualHostNameToFolderMapping(L"app.localhost",web.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));},
   [&](const std::string& name,const Json& value){events.push_back({{"name",name},{"value",value}});if(name=="radial.intent")intents.push_back(value);},
   [&](bool value){owns=value;},[&](bool contact,bool pulse){feedbackTrace.emplace_back(contact,pulse);if(pulse)++feedback;},&input,anchor,[&](bool value){visible=value;},[&]{return feedbackStrength;}
  });
  const std::string nonce="radial-native-private-test-session-a";auto receipt=overlay.create(nonce,payload,"shortcut");const int id=receipt["id"];
  check("window create owns input",owns&&overlay.active());
  check("button summon chooses opposite side",receipt["rect"]["left"].get<int>()>area.left+(area.right-area.left)/2);
  check("built renderer is ready",until([&]{return OverlayTestAccess::ready(overlay)&&visible;},20000));
  check("production renderer has eight primary sectors",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==8;},8000));
  check("real controlled input surface is visible",IsWindowVisible(OverlayTestAccess::input(overlay))!=FALSE);
  const auto scale=OverlayTestAccess::scale(overlay);HRGN region=CreateRectRgn(0,0,0,0);GetWindowRgn(OverlayTestAccess::input(overlay),region);
  check("native input region excludes centre",!PtInRegion(region,(int)(248*scale),(int)(248*scale)));check("native input region includes right sector",PtInRegion(region,(int)(355*scale),(int)(248*scale)));DeleteObject(region);
  check("display remains inert and no-activate",!IsWindowEnabled(OverlayTestAccess::output(overlay))&&(GetWindowLongPtrW(OverlayTestAccess::output(overlay),GWL_EXSTYLE)&WS_EX_NOACTIVATE)!=0);
  RECT rect{};GetWindowRect(OverlayTestAccess::output(overlay),&rect);POINT centre{rect.left+(LONG)(248*scale),rect.top+(LONG)(248*scale)};
  check("Win32 centre hit reaches owned background",WindowFromPoint(centre)==background);
  capture(overlay,out/L"native-main.png");
  ULONGLONG tick=GetTickCount64();overlay.pad(0,0,0,0,0,tick);overlay.pad(0,1,0,0,0,tick+120);overlay.pad(0,1,XINPUT_GAMEPAD_A,0,0,tick+140);
  check("native analog and A open production TDP dial",until([&]{return evaluate(overlay,L"document.querySelectorAll('[data-radial-dial-thumb]').length")==1;}));
  overlay.pad(0,1,0,150,0,tick+260);check("LT adjusts draft without executing",until([&]{return evaluate(overlay,L"document.querySelector('.dial-value')?.textContent")=="14 W";})&&intents.empty());
  capture(overlay,out/L"native-tdp.png");
  overlay.pad(0,1,XINPUT_GAMEPAD_A,0,0,tick+400);check("A confirmation reaches exact native intent receiver",until([&]{return intents.size()==1;})&&intents.back()["intent"]["itemId"]=="tdp"&&intents.back()["intent"]["value"]==14);
  const auto key=intents.back()["intent"]["requestId"];overlay.result(id,nonce,{{"requestId",key},{"ok",true},{"message","owned fixture acknowledged"}});
  check("request-specific result unlocks production controls",until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned fixture acknowledged";}));
  overlay.pad(0,0,0,0,0,tick+500);overlay.pad(0,0,XINPUT_GAMEPAD_B,0,0,tick+520);check("B cancels only dial first",until([&]{return evaluate(overlay,L"document.querySelectorAll('[data-radial-dial-thumb]').length")==0;})&&overlay.active());
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,325.8f,325.8f,1);
  for(int step=1;step<=12;++step)OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Move,325.8f+(437-325.8f)*step/12,325.8f+(287-325.8f)*step/12,1);
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,437,287,1);
  check("continuous synthetic contact crosses real native transport to child",until([&]{return intents.size()==2;})&&intents.back()["intent"]["itemId"]=="thisPc");
  overlay.result(id,nonce,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned child acknowledged"}});
  check("children retain main ring",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==11;}));
  capture(overlay,out/L"native-children.png");
  // Exercise every primary sector through the actual native D-pad consumer.
  tick+=800;overlay.pad(0,0,0,0,0,tick);overlay.pad(0,0,XINPUT_GAMEPAD_B,0,0,tick+20);
  std::vector<std::string> reached;
  const auto selected=[&]{const auto value=evaluate(overlay,L"document.querySelector('.sector-selected .sector-label')?.textContent??''");return value.is_string()?value.get<std::string>():std::string{};};
  const auto direction=[&](WORD button,const char* expected){tick+=150;overlay.pad(0,0,button,0,0,tick);check((std::string("native absolute direction / ")+expected).c_str(),until([&]{return selected()==expected;}));reached.push_back(selected());};
  direction(XINPUT_GAMEPAD_DPAD_RIGHT,"呼出 YMCC");direction(XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_DOWN,"Windows");direction(XINPUT_GAMEPAD_DPAD_DOWN,"监控");direction(XINPUT_GAMEPAD_DPAD_DOWN|XINPUT_GAMEPAD_DPAD_LEFT,"风扇");direction(XINPUT_GAMEPAD_DPAD_LEFT,"设置");direction(XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_UP,"性能调度");direction(XINPUT_GAMEPAD_DPAD_UP,"TDP");direction(XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_RIGHT,"浏览器");
  auto unique=reached;std::sort(unique.begin(),unique.end());unique.erase(std::unique(unique.begin(),unique.end()),unique.end());report["dpadReached"]=reached;check("all eight primary sectors reachable by native D-pad",unique.size()==8);
  tick+=200;overlay.pad(0,1,0,0,0,tick);check("stick selects north before source change",stable([&]{return selected()=="TDP";}));
  tick+=200;overlay.pad(0,0,XINPUT_GAMEPAD_DPAD_RIGHT,0,0,tick);check("D-pad wins over same-frame stick-neutral release",stable([&]{return selected()=="呼出 YMCC";},2000));
  tick+=200;overlay.pad(0,-1,XINPUT_GAMEPAD_A,0,0,tick);
  check("same-frame stick movement selects before A executes",until([&]{return intents.size()==3;})&&intents.back()["intent"]["itemId"]=="monitor");
  overlay.result(id,nonce,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned selection acknowledged"}});
  check("selection acknowledgment completes before next gesture",stable([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned selection acknowledged";}));
  tick+=200;overlay.pad(0,1,0,0,0,tick);check("stick holds north for pointer handover",stable([&]{return selected()=="TDP";}));
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,325.8f,325.8f,2);
  for(int step=1;step<=12;++step)OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Move,325.8f+(437-325.8f)*step/12,325.8f+(287-325.8f)*step/12,2);
  check("actual pointer gesture selects child while stick is held",stable([&]{return selected()=="我的电脑";}));
  tick+=200;overlay.pad(0,1,0,0,0,tick);check("unchanged held stick does not steal touch selection",stable([&]{return selected()=="我的电脑";}));
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,437,287,2);
  check("touch release still executes its selected child",until([&]{return intents.size()==4;})&&intents.back()["intent"]["itemId"]=="thisPc");
  overlay.result(id,nonce,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned handover acknowledged"}});
  evaluate(overlay,L"window.__wheelTestReply=null;window.chrome.webview.addEventListener('message',e=>{if(e.data.id===90001)window.__wheelTestReply=e.data;});window.chrome.webview.postMessage({id:90001,cmd:'shell.execute',args:{wheelId:"+std::to_wstring(id)+L",nonce:'"+ymcc::desktophud::wide(nonce)+L"'}});true");
  check("real renderer cannot access main shell dispatcher",until([&]{const auto r=evaluate(overlay,L"window.__wheelTestReply");return r.is_object()&&r.contains("error");})&&intents.size()==4);
  evaluate(overlay,L"window.__wheelTestReply=null;window.chrome.webview.addEventListener('message',e=>{if(e.data.id===90002)window.__wheelTestReply=e.data;});window.chrome.webview.postMessage({id:90002,cmd:'radial.feedback',args:{wheelId:"+std::to_wstring(id)+L",nonce:'wrong-owner'}});true");
  check("forged nonce cannot pulse feedback",until([&]{const auto r=evaluate(overlay,L"window.__wheelTestReply");return r.is_object()&&r.contains("error");})&&feedback==0);
  check("late frame with wrong session is rejected",!overlay.update(id+1,nonce,payload));check("wrong nonce cannot close current owner",!overlay.close(id,"wrong-owner")&&overlay.active());
  check("prior touch request settles before release gesture",until([&]{return evaluate(overlay,L"document.querySelector('[data-radial-footer=\"1\"]')?.disabled")==false;}));
  const auto beforeRelease=intents.size();tick+=200;overlay.pad(0,0,0,0,0,tick);overlay.pad(0,0,XINPUT_GAMEPAD_DPAD_RIGHT,0,0,tick+10);
  check("direction press selects without executing",stable([&]{return selected()=="呼出 YMCC";})&&intents.size()==beforeRelease);
  overlay.pad(0,0,0,0,0,tick+80);check("native direction release executes exactly once",until([&]{return intents.size()==beforeRelease+1;})&&intents.back()["intent"]["itemId"]=="ymcc");
  overlay.result(id,nonce,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned release acknowledged"}});
  check("release acknowledgment unlocks",until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned release acknowledged";}));
  overlay.pad(0,0,0,0,0,tick+200);check("repeated neutral cannot double execute",stable([&]{return intents.size()==beforeRelease+1;}));
  tick+=400;overlay.pad(0,0,XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_DOWN,0,0,tick);
  check("primary hold starts without children",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-child').length")==0;}));
  overlay.pad(0,0,XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_DOWN,0,0,tick+650);
  check("native duration expands second level at 600ms",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-child').length")==3;})&&intents.size()==beforeRelease+1);
  overlay.pad(0,0,0,0,0,tick+700);check("hold release without child choice does not execute",stable([&]{return intents.size()==beforeRelease+1;}));
  overlay.pad(0,0,XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_UP,0,0,tick+750);overlay.pad(0,0,0,0,0,tick+800);
  check("inward direction restores primary without confirmation",until([&]{return evaluate(overlay,L"document.querySelector('[data-radial-wheel]')?.getAttribute('data-radial-level')")=="primary";})&&intents.size()==beforeRelease+1);
  overlay.pad(1,0,0,0,0,tick+1000);overlay.pad(0,0,0,0,0,tick+1150);overlay.pad(0,0,XINPUT_GAMEPAD_A,0,0,tick+1200);
  check("native stick centre clears candidate without execution",stable([&]{return intents.size()==beforeRelease+1&&selected().empty();}));
  const HWND firstOutput=OverlayTestAccess::output(overlay),firstInput=OverlayTestAccess::input(overlay);
  check("explicit close accepts exact owner",overlay.close(id,nonce));check("close releases both native windows and leases",!owns&&!visible&&!overlay.active()&&!IsWindow(firstOutput)&&!IsWindow(firstInput));
  SetWindowPos(anchor,nullptr,area.right-320,area.top+30,300,400,SWP_NOACTIVATE|SWP_NOZORDER);
  for(const auto* source:{"fullscreen","fullscreen-raw"}){
   const std::string touchNonce=std::string("radial-native-")+source+"-session";
   const auto touchReceipt=overlay.create(touchNonce,payload,source,area.right,area.bottom);
   check((std::string("global touch opposite placement / ")+source).c_str(),touchReceipt["rect"]["left"].get<int>()==area.left+12);
   overlay.close(touchReceipt["id"],touchNonce);
  }
  const std::string second="radial-native-private-test-session-b";receipt=overlay.create(second,payload,"shortcut");const int secondId=receipt["id"];
  check("right anchor summons at left work-area edge",receipt["rect"]["left"].get<int>()==area.left+12);
  check("second actual renderer ready",until([&]{return OverlayTestAccess::ready(overlay)&&visible;},15000));
  SendMessageW(OverlayTestAccess::output(overlay),WM_POWERBROADCAST,PBT_APMSUSPEND,0);
  check("owned-window suspend message releases current session",!overlay.active()&&!owns&&!visible);
  const std::string third="radial-native-private-test-session-c";receipt=overlay.create(third,payload,"monitor",area.left+35,area.top+35);const int thirdId=receipt["id"];
  check("monitor summon stays inside work area",receipt["rect"]["left"].get<int>()>=area.left&&receipt["rect"]["top"].get<int>()>=area.top);
  check("third renderer ready",until([&]{return OverlayTestAccess::ready(overlay)&&visible;},15000));
  OverlayTestAccess::expire(overlay);check("actual native lease timer reclaims expired owner",!overlay.active()&&!owns&&!visible);
  check("late renew cannot resurrect old session",!overlay.renew(thirdId,third));
  const std::string fourth="radial-native-private-test-performance";receipt=overlay.create(fourth,payload,"shortcut");const int fourthId=receipt["id"];
  check("performance renderer ready with actual effective frame",until([&]{return OverlayTestAccess::ready(overlay)&&visible;},15000));
  tick=GetTickCount64();overlay.pad(0,0,0,0,0,tick);
  WORD heldPerf=0;
  const auto pressPerf=[&](WORD button,BYTE left=0,BYTE right=0){overlay.renew(fourthId,fourth);tick+=200;
   if(button&(XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN)){
    if(button==heldPerf)button=button==XINPUT_GAMEPAD_DPAD_RIGHT?XINPUT_GAMEPAD_DPAD_DOWN:XINPUT_GAMEPAD_DPAD_UP;
    heldPerf=button;overlay.pad(0,0,button,left,right,tick);
   }else{overlay.pad(0,0,button|heldPerf,left,right,tick);if(button&(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B)){heldPerf=0;overlay.pad(0,0,0,0,0,tick+1);}}
  };
  tick+=200;overlay.pad(-.7071f,.7071f,0,0,0,tick);overlay.pad(-.7071f,.7071f,XINPUT_GAMEPAD_A,0,0,tick+20);
  check("native performance entry extends existing main ring",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==12;}));
  const auto selectedControl=[&]{return evaluate(overlay,L"document.querySelector('.sector-selected')?.getAttribute('data-radial-control')??''");};
  for(const auto* key:{"tdp","mode","bias"}){pressPerf(XINPUT_GAMEPAD_DPAD_RIGHT);check((std::string("native controller reaches performance ")+key).c_str(),until([&]{return selectedControl()==key;}));}
  const auto beforePerformance=intents.size();pressPerf(XINPUT_GAMEPAD_A);
  check("native performance opens a bounded preference draft",until([&]{return evaluate(overlay,L"Number(document.querySelector('[data-radial-draft]')?.getAttribute('data-radial-draft-value'))")==25;}));
  pressPerf(0,0,150);check("native RT uses the original preference step",until([&]{return evaluate(overlay,L"Number(document.querySelector('[data-radial-draft]')?.getAttribute('data-radial-draft-value'))")==30;})&&intents.size()==beforePerformance);
  pressPerf(XINPUT_GAMEPAD_B);check("native B discards preference draft without a control intent",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==12;})&&intents.size()==beforePerformance);
  pressPerf(XINPUT_GAMEPAD_DPAD_LEFT);check("native mode remains reachable after cancel",until([&]{return selectedControl()=="mode";}));pressPerf(XINPUT_GAMEPAD_A);
  check("native mode uses a choice draft",until([&]{return evaluate(overlay,L"document.querySelector('[data-radial-draft]')?.textContent")=="自动模式";}));
  pressPerf(0,0,150);check("native RT chooses manual without executing",until([&]{return evaluate(overlay,L"document.querySelector('[data-radial-draft]')?.textContent")=="手动模式";})&&intents.size()==beforePerformance);
  pressPerf(XINPUT_GAMEPAD_A);check("actual output forwards exact performance control and source revision",until([&]{return intents.size()==beforePerformance+1;})&&intents.back()["intent"]["itemId"]=="pages"&&intents.back()["intent"]["control"]=="mode"&&intents.back()["intent"]["value"]=="manual"&&intents.back()["intent"]["controlRevision"]=="native-performance-auto");
  overlay.result(fourthId,fourth,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned manual mode acknowledged"},{"performance",payload.at("fixturePerformanceManual")}});
  check("actual result switches to six manual controls and keeps the main ring",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==14;}));
  const auto frequency=evaluate(overlay,L"(()=>{const el=document.querySelector('[data-radial-control=frequencyMHz] .sector-label');return [Number(el.getAttribute('x')),Number(el.getAttribute('y'))-16]})()");
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,frequency[0].get<float>(),frequency[1].get<float>(),7);OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,frequency[0].get<float>(),frequency[1].get<float>(),7);
  check("native touch reaches manual frequency",until([&]{return evaluate(overlay,L"Number(document.querySelector('[data-radial-draft]')?.getAttribute('data-radial-draft-value'))")==4000;}));
  pressPerf(0,0,150);check("manual native frequency step is 100 MHz",until([&]{return evaluate(overlay,L"Number(document.querySelector('[data-radial-draft]')?.getAttribute('data-radial-draft-value'))")==4100;}));
  pressPerf(XINPUT_GAMEPAD_A);check("manual control intent reaches native with current frame revision",until([&]{return intents.size()==beforePerformance+2;})&&intents.back()["intent"]["control"]=="frequencyMHz"&&intents.back()["intent"]["value"]==4100&&intents.back()["intent"]["controlRevision"]=="native-performance-manual");
  overlay.result(fourthId,fourth,{{"requestId","unrelated-request"},{"ok",true},{"message","must not unlock"}});
  check("unrelated result cannot unlock performance Apply",stable([&]{return evaluate(overlay,L"document.querySelector('[data-radial-footer=\"1\"]')?.disabled")==true;}));
  auto refreshedPerformance=payload.at("fixturePerformanceManual");refreshedPerformance["sourceRevision"]="native-performance-updated";for(auto& control:refreshedPerformance["controls"])if(control["key"]=="frequencyMHz")control["value"]=4100;
  overlay.result(fourthId,fourth,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned frequency saved"},{"performance",refreshedPerformance}});
  check("matching result closes the stale draft and refreshes controls",until([&]{return evaluate(overlay,L"document.querySelectorAll('[data-radial-draft]').length")==0&&evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned frequency saved";}));
  capture(overlay,out/L"native-performance-manual.png");
  HRGN performanceRegion=CreateRectRgn(0,0,0,0);GetWindowRgn(OverlayTestAccess::input(overlay),performanceRegion);const auto performanceScale=OverlayTestAccess::scale(overlay);
  check("manual performance controls keep the native centre hole",!PtInRegion(performanceRegion,(int)(248*performanceScale),(int)(248*performanceScale)));DeleteObject(performanceRegion);
  check("performance close returns all native input leases",overlay.close(fourthId,fourth)&&!overlay.active()&&!owns&&!visible);
  auto fanPayload=payload;fanPayload["settings"]["pages"][0]["items"][6]={{"id","fanControls"},{"label","风扇"},{"action","fan"},{"enabled",true}};
  fanPayload["settings"]["pages"][0]["items"][3]={{"id","fanPage"},{"label","风扇设置"},{"action","page.fan"},{"enabled",true}};
  const std::string fifth="radial-native-private-test-fan";receipt=overlay.create(fifth,fanPayload,"shortcut");const int fifthId=receipt["id"];
  check("fan renderer ready at the new default north position",until([&]{return OverlayTestAccess::ready(overlay)&&visible;},15000));
  tick=GetTickCount64();overlay.pad(0,0,0,0,0,tick);
  WORD heldFan=0;
  const auto pressFan=[&](WORD button){overlay.renew(fifthId,fifth);tick+=200;
   if(button&(XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN)){
    if(button==heldFan)button=button==XINPUT_GAMEPAD_DPAD_RIGHT?XINPUT_GAMEPAD_DPAD_DOWN:XINPUT_GAMEPAD_DPAD_UP;
    heldFan=button;overlay.pad(0,0,button,0,0,tick);
   }else{overlay.pad(0,0,button|heldFan,0,0,tick);if(button&(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B)){heldFan=0;overlay.pad(0,0,0,0,0,tick+1);}}
  };
  tick+=200;overlay.pad(0,1,0,0,0,tick);
  check("native stick reaches fan north",until([&]{return evaluate(overlay,L"document.querySelector('.sector-selected')?.getAttribute('data-radial-sector')")=="fanControls";}));
  tick+=200;overlay.pad(0,1,XINPUT_GAMEPAD_A,0,0,tick);check("fan children extend the original ring",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==12&&evaluate(overlay,L"document.querySelectorAll('[data-radial-fan]').length")==4&&evaluate(overlay,L"document.querySelectorAll('.sector-selected').length")==0;}));
  const auto selectedFan=[&](){return evaluate(overlay,L"document.querySelector('.sector-selected[data-radial-fan]')?.getAttribute('data-radial-fan')");};
  for(const auto* choice:{"off","soft","balanced","aggressive"}){
    for(int step=0;step<4&&selectedFan()!=choice;step++){const auto priorFan=selectedFan();pressFan(XINPUT_GAMEPAD_DPAD_RIGHT);ensure(until([&]{return selectedFan()!=priorFan;},1500),"Fan direction did not produce a rendered selection");}
    check((std::string("native controller reaches fan ")+choice).c_str(),until([&]{return selectedFan()==choice;}));
    const auto beforeFan=intents.size();pressFan(XINPUT_GAMEPAD_A);
    check((std::string("authenticated exact fan choice ")+choice).c_str(),until([&]{return intents.size()==beforeFan+1;})&&intents.back()["intent"]["itemId"]=="fanControls"&&intents.back()["intent"]["fanChoice"]==choice&&intents.back()["intent"]["fanRevision"]==fanPayload["fan"]["sourceRevision"]);
    fanPayload["fan"]["sourceRevision"]=std::string("fan-result-")+choice;fanPayload["fan"]["choice"]=choice;fanPayload["fan"]["active"]=std::string(choice)!="off";fanPayload["fan"]["confirmed"]=std::string(choice)!="off";
    overlay.result(fifthId,fifth,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message",std::string("owned fan result ")+choice},{"fan",fanPayload["fan"]}});
    check((std::string("matching fan receipt refreshes ")+choice).c_str(),until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")==std::string("owned fan result ")+choice;}));
  }
  fanPayload["fan"]["sourceRevision"]="fan-live-refresh";fanPayload["fan"]["notice"]="owned fan live state";overlay.update(fifthId,fifth,fanPayload);
  check("late original-owner fan state replaces pending notice",until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned fan live state";}));
  const auto softFan=evaluate(overlay,L"(()=>{const el=document.querySelector('[data-radial-fan=soft] .sector-label');return [Number(el.getAttribute('x')),Number(el.getAttribute('y'))-16]})()");
  const auto beforeFanTouch=intents.size();OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,softFan[0].get<float>(),softFan[1].get<float>(),93);
  fanPayload["fan"]["sourceRevision"]="fan-external-revision";overlay.update(fifthId,fifth,fanPayload);
  check("fan frame changes while a controlled contact is held",until([&]{return evaluate(overlay,L"document.querySelectorAll('.sector-selected[data-radial-fan]').length")==0;}));
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,softFan[0].get<float>(),softFan[1].get<float>(),93);
  check("late fan touch release cannot submit an old revision",stable([&]{return intents.size()==beforeFanTouch;}));
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,softFan[0].get<float>(),softFan[1].get<float>(),94);OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,softFan[0].get<float>(),softFan[1].get<float>(),94);
  check("native touch submits current exact fan choice",until([&]{return intents.size()==beforeFanTouch+1;})&&intents.back()["intent"]["fanChoice"]=="soft"&&intents.back()["intent"]["fanRevision"]=="fan-external-revision");
  HRGN fanRegion=CreateRectRgn(0,0,0,0);GetWindowRgn(OverlayTestAccess::input(overlay),fanRegion);const auto fanScale=OverlayTestAccess::scale(overlay);check("fan native input region retains centre hole",!PtInRegion(fanRegion,(int)(248*fanScale),(int)(248*fanScale)));DeleteObject(fanRegion);
  capture(overlay,out/L"native-fan.png");check("fan close returns all input leases",overlay.close(fifthId,fifth)&&!overlay.active()&&!owns&&!visible);
  auto inputPayload=payload;inputPayload["settings"]["pages"][0]["items"][3]={{"id","virtualPad"},{"label","虚拟手柄"},{"action","input.pad"},{"enabled",true}};
  inputPayload["settings"]["pages"][0]["items"][4]={{"id","gyro"},{"label","陀螺仪"},{"action","input.gyro"},{"enabled",true}};
  inputPayload["settings"]["pages"][0]["items"][6]={{"id","fanControls"},{"label","风扇"},{"action","fan"},{"enabled",true}};
  const std::string sixth="radial-native-private-test-input";receipt=overlay.create(sixth,inputPayload,"shortcut");const int sixthId=receipt["id"];
  check("input renderer ready with original local choice identities",until([&]{return OverlayTestAccess::ready(overlay)&&visible;},15000));
  tick=GetTickCount64();overlay.pad(0,0,0,0,0,tick);
  WORD heldInput=0;
  const auto pressInput=[&](WORD button){overlay.renew(sixthId,sixth);tick+=200;
   if(button&(XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN)){
    if(button==heldInput)button=button==XINPUT_GAMEPAD_DPAD_RIGHT?XINPUT_GAMEPAD_DPAD_DOWN:XINPUT_GAMEPAD_DPAD_UP;
    heldInput=button;overlay.pad(0,0,button,0,0,tick);
   }else{overlay.pad(0,0,button|heldInput,0,0,tick);if(button&(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B)){heldInput=0;overlay.pad(0,0,0,0,0,tick+1);}}
  };
  const auto enterInput=[&](float x,float y,const std::string& id){overlay.renew(sixthId,sixth);tick+=200;overlay.pad(0,0,0,0,0,tick);tick+=200;overlay.pad(x,y,0,0,0,tick);ensure(until([&]{return evaluate(overlay,L"document.querySelector('.sector-selected')?.getAttribute('data-radial-sector')")==id;}),"input parent not selected");overlay.pad(x,y,XINPUT_GAMEPAD_A,0,0,tick+20);ensure(until([&]{return evaluate(overlay,L"document.querySelectorAll('.sector-selected').length")==0;}),"input parent not activated");};
  const auto selectedInput=[&](){return evaluate(overlay,L"document.querySelector('.sector-selected[data-radial-input]')?.getAttribute('data-radial-input')");};
  const auto reachInput=[&](const std::string& target){for(int step=0;step<6&&selectedInput()!=target;step++){const auto priorInput=selectedInput();pressInput(XINPUT_GAMEPAD_DPAD_RIGHT);ensure(until([&]{return selectedInput()!=priorInput;},1500),"input direction did not update selected child");}check(("native input reaches "+target).c_str(),selectedInput()==target);};
  const auto acceptInput=[&](const std::string& parent,const std::string& field,const std::string& value){const auto before=intents.size();pressInput(XINPUT_GAMEPAD_A);check(("exact authenticated input "+field+":"+value).c_str(),until([&]{return intents.size()==before+1;})&&intents.back()["intent"]["itemId"]==parent&&intents.back()["intent"]["inputField"]==field&&intents.back()["intent"]["inputValue"]==value&&intents.back()["intent"]["inputRevision"]==inputPayload["input"]["sourceRevision"]);
    for(auto& control:inputPayload["input"]["controls"]){if(control["key"]==field)control["value"]=value;if(field=="gyroEnabled"&&control["key"]=="gyroPreset")control["enabled"]=value=="on";}
    inputPayload["input"]["sourceRevision"]="input-result-"+field+":"+value;inputPayload["input"]["notice"]="owned input result "+value;
    overlay.result(sixthId,sixth,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned input result "+value},{"input",inputPayload["input"]}});
    ensure(until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned input result "+value;}),"input receipt did not update renderer");};
  enterInput(-.7071f,-.7071f,"virtualPad");check("pad OFF and three personas extend main ring",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==12&&evaluate(overlay,L"document.querySelector('[data-radial-input-scope]')?.textContent")=="原生夹具 · 全局输入";}));
  for(const auto* persona:{"disabled","steamdeck","dualsense-edge","elite"}){reachInput(std::string("padPersona:")+persona);acceptInput("virtualPad","padPersona",persona);}
  pressInput(XINPUT_GAMEPAD_B);enterInput(-1,0,"gyro");check("gyro switch and four presets extend the same primary ring",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==14;}));
  for(const auto* value:{"off","on"}){reachInput(std::string("gyroEnabled:")+value);acceptInput("gyro","gyroEnabled",value);if(std::string(value)=="off")check("OFF excludes four preset controls",until([&]{return evaluate(overlay,L"document.querySelectorAll('[data-radial-input^=\"gyroPreset:\"].sector-disabled').length")==4;}));}
  for(const auto* preset:{"fps","racing","custom","steam"}){reachInput(std::string("gyroPreset:")+preset);acceptInput("gyro","gyroPreset",preset);}
  const auto gyroTouch=evaluate(overlay,L"(()=>{const el=document.querySelector('[data-radial-input=\"gyroPreset:custom\"] .sector-label');return [Number(el.getAttribute('x')),Number(el.getAttribute('y'))-16]})()");const auto beforeInputTouch=intents.size();
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,gyroTouch[0].get<float>(),gyroTouch[1].get<float>(),111);
  inputPayload["input"]["scope"]="game";inputPayload["input"]["label"]="原生夹具 · game.exe 游戏专属";inputPayload["input"]["sourceRevision"]="input-game-changed";inputPayload["input"]["notice"]="owned game scope changed";overlay.update(sixthId,sixth,inputPayload);
  check("native scope and notice follow the original current-game frame",until([&]{return evaluate(overlay,L"document.querySelector('[data-radial-input-scope]')?.textContent")=="原生夹具 · game.exe 游戏专属"&&evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned game scope changed";}));
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,gyroTouch[0].get<float>(),gyroTouch[1].get<float>(),111);check("old held input contact cannot submit after game change",stable([&]{return intents.size()==beforeInputTouch;}));
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,gyroTouch[0].get<float>(),gyroTouch[1].get<float>(),112);OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,gyroTouch[0].get<float>(),gyroTouch[1].get<float>(),112);
  check("native touch submits current gyro preset and revision",until([&]{return intents.size()==beforeInputTouch+1;})&&intents.back()["intent"]["inputField"]=="gyroPreset"&&intents.back()["intent"]["inputValue"]=="custom"&&intents.back()["intent"]["inputRevision"]=="input-game-changed");
  overlay.result(sixthId,sixth,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned input touch acknowledged"},{"input",inputPayload["input"]}});ensure(until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned input touch acknowledged";}),"input touch acknowledgment missing");
  pressInput(XINPUT_GAMEPAD_B);enterInput(-.7071f,-.7071f,"virtualPad");inputPayload["input"]["controls"][0]["choices"][1]["enabled"]=false;inputPayload["input"]["sourceRevision"]="input-machine-blocked";overlay.update(sixthId,sixth,inputPayload);
  check("machine-blocked persona is disabled in the actual native renderer",until([&]{return evaluate(overlay,L"document.querySelector('[data-radial-input=\"padPersona:steamdeck\"]')?.classList.contains('sector-disabled')")==true;}));
  const auto blockedTouch=evaluate(overlay,L"(()=>{const el=document.querySelector('[data-radial-input=\"padPersona:steamdeck\"] .sector-label');return [Number(el.getAttribute('x')),Number(el.getAttribute('y'))-16]})()");const auto beforeBlockedTouch=intents.size();
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,blockedTouch[0].get<float>(),blockedTouch[1].get<float>(),113);OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,blockedTouch[0].get<float>(),blockedTouch[1].get<float>(),113);check("blocked native touch cannot issue an input request",stable([&]{return intents.size()==beforeBlockedTouch;}));reachInput("padPersona:dualsense-edge");
  HRGN inputRegion=CreateRectRgn(0,0,0,0);GetWindowRgn(OverlayTestAccess::input(overlay),inputRegion);const auto inputScale=OverlayTestAccess::scale(overlay);check("input children preserve native centre hole",!PtInRegion(inputRegion,(int)(248*inputScale),(int)(248*inputScale)));DeleteObject(inputRegion);
  capture(overlay,out/L"native-input.png");check("input close returns all native input leases",overlay.close(sixthId,sixth)&&!overlay.active()&&!owns&&!visible);
  auto utilityPayload=payload;utilityPayload["settings"]=payload.at("fixtureCurrentSettings");
  const std::string seventh="radial-native-private-test-utilities";receipt=overlay.create(seventh,utilityPayload,"shortcut");const int seventhId=receipt["id"];
  check("final-menu utility renderer ready",until([&]{return OverlayTestAccess::ready(overlay)&&visible;},15000));tick=GetTickCount64();overlay.pad(0,0,0,0,0,tick);
  WORD heldUtility=0;
  const auto pressUtility=[&](WORD button){overlay.renew(seventhId,seventh);tick+=200;
   if(button&(XINPUT_GAMEPAD_DPAD_LEFT|XINPUT_GAMEPAD_DPAD_RIGHT|XINPUT_GAMEPAD_DPAD_UP|XINPUT_GAMEPAD_DPAD_DOWN)){
    if(button==heldUtility)button=button==XINPUT_GAMEPAD_DPAD_RIGHT?XINPUT_GAMEPAD_DPAD_DOWN:XINPUT_GAMEPAD_DPAD_UP;
    heldUtility=button;overlay.pad(0,0,button,0,0,tick);
   }else{overlay.pad(0,0,button|heldUtility,0,0,tick);if(button&(XINPUT_GAMEPAD_A|XINPUT_GAMEPAD_B)){heldUtility=0;overlay.pad(0,0,0,0,0,tick+1);}}
  };
  const auto enterUtility=[&](float x,float y,const std::string& id){overlay.renew(seventhId,seventh);tick+=200;overlay.pad(0,0,0,0,0,tick);tick+=200;overlay.pad(x,y,0,0,0,tick);ensure(until([&]{return evaluate(overlay,L"document.querySelector('.sector-selected')?.getAttribute('data-radial-sector')")==id;}),"utility parent not selected");overlay.pad(x,y,XINPUT_GAMEPAD_A,0,0,tick+20);ensure(until([&]{return evaluate(overlay,L"document.querySelectorAll('.sector-selected').length")==0;}),"utility parent not activated");};
  const auto selectedUtility=[&](){return evaluate(overlay,L"document.querySelector('.sector-selected[data-radial-utility]')?.getAttribute('data-radial-utility')");};
  const auto reachUtility=[&](const std::string& choice){for(int step=0;step<3&&selectedUtility()!=choice;step++){const auto prior=selectedUtility();pressUtility(XINPUT_GAMEPAD_DPAD_RIGHT);ensure(until([&]{return selectedUtility()!=prior;},1500),"utility direction did not update");}check(("native controller reaches utility "+choice).c_str(),selectedUtility()==choice);};
  const auto submitUtility=[&](const std::string& id,const std::string& group,const std::string& choice){const auto before=intents.size();pressUtility(XINPUT_GAMEPAD_A);check(("exact utility metadata "+choice).c_str(),until([&]{return intents.size()==before+1;})&&intents.back()["intent"]["itemId"]==id&&intents.back()["intent"]["utilityChoice"]==choice&&intents.back()["intent"]["utilityRevision"]==utilityPayload["utilities"][group]["sourceRevision"]);};
  enterUtility(0,-1,"monitor");check("native A/B extends the main ring",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==10&&evaluate(overlay,L"document.querySelectorAll('[data-radial-utility]').length")==2;}));
  reachUtility("B");submitUtility("monitor","monitor.templates","B");utilityPayload["utilities"]["monitor.templates"]["selected"]="B";utilityPayload["utilities"]["monitor.templates"]["sourceRevision"]="utility-monitor-B";
  overlay.result(seventhId,seventh,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned monitor B saved"},{"utilities",utilityPayload["utilities"]}});ensure(until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned monitor B saved";}),"template B receipt missing");
  const auto templatePoint=evaluate(overlay,L"(()=>{const el=document.querySelector('[data-radial-utility=A] .sector-label');return [Number(el.getAttribute('x')),Number(el.getAttribute('y'))-16]})()");const auto beforeTemplateTouch=intents.size();
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,templatePoint[0].get<float>(),templatePoint[1].get<float>(),121);utilityPayload["utilities"]["monitor.templates"]["sourceRevision"]="utility-monitor-external";overlay.update(seventhId,seventh,utilityPayload);ensure(until([&]{return evaluate(overlay,L"document.querySelectorAll('.sector-selected').length")==0;}),"template revision did not retire held selection");
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,templatePoint[0].get<float>(),templatePoint[1].get<float>(),121);check("old template touch cannot commit after external edit",stable([&]{return intents.size()==beforeTemplateTouch;}));
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,templatePoint[0].get<float>(),templatePoint[1].get<float>(),122);utilityPayload["utilities"]["steam.controls"]["sourceRevision"]="utility-unrelated-steam";overlay.update(seventhId,seventh,utilityPayload);pump();
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Up,templatePoint[0].get<float>(),templatePoint[1].get<float>(),122);check("unrelated Steam frame leaves a valid monitor touch intact",until([&]{return intents.size()==beforeTemplateTouch+1;})&&intents.back()["intent"]["utilityChoice"]=="A"&&intents.back()["intent"]["utilityRevision"]=="utility-monitor-external");
  overlay.result(seventhId,seventh,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned template touch accepted"},{"utilities",utilityPayload["utilities"]}});ensure(until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned template touch accepted";}),"template touch result missing");
  pressUtility(XINPUT_GAMEPAD_B);enterUtility(.7071f,.7071f,"steam");check("Steam three actions extend the original ring",until([&]{return evaluate(overlay,L"document.querySelectorAll('.wheel-sector').length")==11&&evaluate(overlay,L"document.querySelectorAll('[data-radial-utility]').length")==3;}));
  reachUtility("bigpicture");submitUtility("steam","steam.controls","bigpicture");auto& steamUtility=utilityPayload["utilities"]["steam.controls"];steamUtility["selected"]="bigpicture";steamUtility["sourceRevision"]="utility-bigpicture";
  overlay.result(seventhId,seventh,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned Big Picture observed"},{"utilities",utilityPayload["utilities"]}});ensure(until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned Big Picture observed";}),"Big Picture result missing");
  reachUtility("stop");submitUtility("steam","steam.controls","stop");steamUtility["busy"]=true;steamUtility["pending"]=true;steamUtility["sourceRevision"]="utility-stop-submitted";for(auto& choice:steamUtility["choices"])choice["enabled"]=false;
  overlay.result(seventhId,seventh,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned graceful stop submitted"},{"utilities",utilityPayload["utilities"]}});check("stop submission disables repeated commands without claiming stopped",until([&]{return evaluate(overlay,L"document.querySelectorAll('[data-radial-utility].sector-disabled').length")==3&&evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned graceful stop submitted";}));
  const auto afterStop=intents.size();steamUtility["busy"]=false;steamUtility["pending"]=false;steamUtility["selected"]="stop";steamUtility["sourceRevision"]="utility-stop-observed";steamUtility["notice"]="owned stopped observation";for(auto& choice:steamUtility["choices"])choice["enabled"]=choice["value"]!="stop";overlay.update(seventhId,seventh,utilityPayload);
  check("actual subsequent utility frame refreshes stop status without command replay",until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned stopped observation";})&&intents.size()==afterStop);
  reachUtility("start");submitUtility("steam","steam.controls","start");steamUtility["sourceRevision"]="utility-start-observed";steamUtility["selected"]="bigpicture";steamUtility["notice"]="owned launch observed";for(auto& choice:steamUtility["choices"])choice["enabled"]=choice["value"]!="start";
  overlay.result(seventhId,seventh,{{"requestId",intents.back()["intent"]["requestId"]},{"ok",true},{"message","owned launch observed"},{"utilities",utilityPayload["utilities"]}});ensure(until([&]{return evaluate(overlay,L"document.querySelector('.wheel-status')?.textContent")=="owned launch observed";}),"launch result missing");
  HRGN utilityRegion=CreateRectRgn(0,0,0,0);GetWindowRgn(OverlayTestAccess::input(overlay),utilityRegion);const auto utilityScale=OverlayTestAccess::scale(overlay);check("utility native input region preserves centre hole",!PtInRegion(utilityRegion,(int)(248*utilityScale),(int)(248*utilityScale)));DeleteObject(utilityRegion);
  capture(overlay,out/L"native-utilities.png");check("utility close releases the actual native input leases",overlay.close(seventhId,seventh)&&!overlay.active()&&!owns&&!visible);



  auto tactilePayload=payload;tactilePayload["feedback"]=true;tactilePayload["settings"]["feedback"]=true;
  const std::string tactileNonce="radial-native-gesture-and-animation";auto tactile=overlay.create(tactileNonce,tactilePayload,"monitor",area.left+120,area.top+160);const int tactileId=tactile["id"];
  ensure(until([&]{return OverlayTestAccess::ready(overlay)&&visible;}),"tactile renderer missing");
  check("opening carries native monitor origin and duration",until([&]{return evaluate(overlay,L"getComputedStyle(document.querySelector('.radial-output')).getPropertyValue('--radial-enter-ms').trim()") == "240ms";}));
  check("opening uses the actual wheel keyframes",until([&]{return evaluate(overlay,L"document.querySelector('.radial-wheel').getAnimations().some(a=>a.animationName==='wheel-open')")==true;}));
  const auto tactileBefore=feedback;
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,320,248,91);
  check("native touch down establishes one existing-channel pulse",feedback==tactileBefore+1&&feedbackTrace.back().first);
  const auto stationary=feedback;OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Move,320,248,91);
  check("native stationary gesture does not pulse by selection",feedback==stationary);
  const auto waitAt=GetTickCount64();until([&]{return GetTickCount64()-waitAt>=50;},100);
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Move,350,248,91);
  check("native slide within same sector uses touchpad distance cadence",feedback==stationary+1);
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Cancel,350,248,91,true);
  check("native cancelled slide clears contact immediately",!feedbackTrace.back().first&&!feedbackTrace.back().second);
  const auto fixed=feedback;ensure(until([&]{return evaluate(overlay,L"document.querySelector('.radial-output').classList.contains('open')")==true;}),"tactile view not open");
  pump();check("pointer selection adds no second renderer pulse",feedback==fixed);
  OverlayTestAccess::pointer(overlay,ymcc::screeninput::Phase::Down,320,248,92);
  check("dismiss requests exact live owner",overlay.dismiss(tactileId,tactileNonce));
  check("dismiss retains renderer and ownership for its animation",overlay.active()&&owns&&visible&&OverlayTestAccess::closing(overlay)&&!IsWindowVisible(OverlayTestAccess::input(overlay)));
  check("dismiss cancels feedback before its exit animation",!feedbackTrace.back().first&&!feedbackTrace.back().second);
  check("closing frame cannot reactivate input",!overlay.update(tactileId,tactileNonce,tactilePayload));
  ensure(until([&]{return evaluate(overlay,L"document.querySelector('.radial-output').classList.contains('closing')")==true;}),"exit phase missing");
  evaluate(overlay,L"document.querySelector('.radial-wheel').getAnimations().filter(a=>a.animationName==='wheel-close').forEach(a=>a.finish());true");
  check("renderer exit acknowledgement releases native owner",until([&]{return !overlay.active();},3000)&&!owns&&!visible&&events.back()["value"]["reason"]=="animated-close");
  const std::string fallbackNonce="radial-native-exit-fallback-owned";auto fallback=overlay.create(fallbackNonce,tactilePayload,"shortcut");ensure(until([&]{return OverlayTestAccess::ready(overlay)&&visible;}),"fallback renderer missing");
  evaluate(overlay,L"const original=window.chrome.webview.postMessage.bind(window.chrome.webview);window.chrome.webview.postMessage=v=>{if(v.cmd!=='radial.transition.complete')original(v)};true");
  overlay.dismiss(fallback["id"],fallbackNonce);
  check("missing renderer exit acknowledgement has bounded native cleanup",until([&]{return !overlay.active();},3000)&&!owns&&!visible&&events.back()["value"]["reason"]=="close-animation-timeout");

  {
   using ymcc::desktophud::Carrier;using ymcc::desktophud::CarrierTestAccess;
   Carrier monitor;ymcc::desktophud::Config cfg;bool monitorReady=false;
   cfg.serve=[&](ICoreWebView2* view){ComPtr<ICoreWebView2_3> map;return SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&map)))&&SUCCEEDED(map->SetVirtualHostNameToFolderMapping(L"app.localhost",web.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));};
   cfg.notify=[&](const std::string& event,const Json& value){if(event=="desktopHud.ready")monitorReady=true;};cfg.anchor=anchor;cfg.inputRuntime=&input;
   monitor.initialize(environment.Get(),std::move(cfg));const std::string monitorNonce="radial-owned-monitor-transition";auto handle=monitor.create(0,0,monitorNonce,true);const int monitorId=handle["id"];
   check("actual monitor renderer handshakes on private desktop",until([&]{return monitorReady;},15000));
   auto monitorFrame=payload.at("fixtureHud");monitorFrame["hudId"]=monitorId;monitorFrame["nonce"]=monitorNonce;monitorFrame["seq"]=1;monitorFrame["expiresAt"]=ymcc::desktophud::wallNow()+6000;
   check("actual monitor frame is admitted",monitor.frame(monitorId,monitorNonce,monitorFrame));
   check("actual monitor renderer paints its own native window",until([&]{return monitor.status().value("visible",false);},10000));
   auto hud=[&](const std::wstring& js){return evaluateView(CarrierTestAccess::view(monitor),js);};
   monitor.suspendForRadial(true);
   check("monitor starts morph while window is still visible",IsWindowVisible(CarrierTestAccess::window(monitor)));
   check("monitor input is withdrawn at morph start",!IsWindowVisible(CarrierTestAccess::input(monitor)));
   check("actual monitor renderer receives shrink animation",until([&]{return hud(L"document.querySelector('#hud').classList.contains('radial-monitor-away')")==true;}));
   check("monitor native window hides after bounded fade",until([&]{return !monitor.status().value("visible",true);},1500));
   monitor.suspendForRadial(false);
   check("monitor restores through the original visibility policy",monitor.status().value("visible",false));
   check("actual monitor renderer receives return animation",until([&]{return hud(L"document.querySelector('#hud').classList.contains('radial-monitor-return')")==true;}));
   monitor.suspendForRadial(true);monitor.suspendForRadial(false);const auto restoreAt=GetTickCount64();until([&]{return GetTickCount64()-restoreAt>=300;},1000);
   check("late morph timer cannot hide an already restored monitor",monitor.status().value("visible",false));
   check("monitor transition owner closes cleanly",monitor.close(monitorId,monitorNonce));
  }

  report["nativeEvents"]=events;report["intents"]=intents;report["pass"]=true;
 }catch(const std::exception& e){failure=e.what();report["error"]=failure;report["win32Error"]=(unsigned long)GetLastError();report["inputRuntimeHr"]=(long long)input.error;report["environmentHr"]=(long long)envError;try{if(overlay.active())report["rendererDebug"]=evaluate(overlay,L"({phase:document.querySelector('.radial-output')?.className,animation:getComputedStyle(document.querySelector('.radial-wheel')).animationName,animations:document.querySelector('.radial-wheel').getAnimations().map(a=>({name:a.animationName,state:a.playState,time:a.currentTime}))})");}catch(...){} }
 overlay.shutdown();environment.Reset();if(anchor)DestroyWindow(anchor);if(background)DestroyWindow(background);input.compositor=nullptr;input.queue=nullptr;if(initialized)CoUninitialize();
 std::ofstream saved(out/L"native-overlay.json",std::ios::binary);saved<<report.dump(2);std::cout<<Json{{"pass",report["pass"]},{"cases",report["cases"].size()},{"error",failure}}.dump()<<"\n";return report.value("pass",false)&&saved.good()?0:3;
}
static std::wstring quoted(const std::wstring& s){if(s.find(L'"')!=std::wstring::npos)throw std::runtime_error("Quoted path unsupported");return L"\""+s+L"\"";}
int wmain(int argc,wchar_t** argv){
 SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
 if(argc==5&&std::wstring(argv[1])==L"--private-worker")return worker(argv[2],argv[3],argv[4]);
 if(argc!=4){std::cerr<<"usage: radial_native_selftest WEB_ROOT NEW_OUT FRAME_FILE\n";return 2;}
 const auto before=GetForegroundWindow();POINT cursorBefore{};GetCursorPos(&cursorBefore);
 HDESK desk=nullptr;HANDLE job=nullptr;PROCESS_INFORMATION child{};int exitCode=3;Json launch={{"pass",false},{"privateDesktop",true},{"desktopSwitched",false},{"systemInputInjected",false},{"normalYmccStarted",false}};
 const std::filesystem::path out=argv[2];std::filesystem::create_directories(out);
 try{
  wchar_t station[256]{};DWORD bytes=0;ensure(GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station,sizeof(station),&bytes)!=FALSE,"Window station name unavailable");
  std::wstring name=L"YMCC_Radial_Isolated_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
  desk=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);ensure(desk!=nullptr,"Private desktop creation failed");
  job=CreateJobObjectW(nullptr,nullptr);ensure(job!=nullptr,"Owned process job unavailable");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;ensure(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits))!=FALSE,"Owned process cleanup policy unavailable");
  wchar_t exe[32768]{};GetModuleFileNameW(nullptr,exe,32768);std::wstring command=quoted(exe)+L" --private-worker "+quoted(argv[1])+L" "+quoted(argv[2])+L" "+quoted(argv[3]);std::wstring desktop=std::wstring(station)+L"\\"+name;
  STARTUPINFOW startup{sizeof(startup)};startup.lpDesktop=desktop.data();ensure(CreateProcessW(exe,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&startup,&child)!=FALSE,"Private worker start failed");
  if(!AssignProcessToJobObject(job,child.hProcess)){TerminateProcess(child.hProcess,4);throw std::runtime_error("Cannot bind owned worker cleanup");}ResumeThread(child.hThread);launch["workerPid"]=child.dwProcessId;
  const auto end=GetTickCount64()+120000;while(WaitForSingleObject(child.hProcess,1000)==WAIT_TIMEOUT){if(GetTickCount64()>=end){TerminateJobObject(job,5);throw std::runtime_error("Owned native UI test timed out");}}
  DWORD code=3;GetExitCodeProcess(child.hProcess,&code);exitCode=(int)code;launch["workerExitCode"]=code;launch["pass"]=code==0;
 }catch(const std::exception& e){launch["error"]=e.what();launch["win32Error"]=(unsigned long)GetLastError();}
 if(job)CloseHandle(job);if(child.hThread)CloseHandle(child.hThread);if(child.hProcess)CloseHandle(child.hProcess);if(desk)CloseDesktop(desk);
 POINT cursorAfter{};GetCursorPos(&cursorAfter);launch["userForegroundUnchanged"]=GetForegroundWindow()==before;launch["userCursorUnchanged"]=cursorBefore.x==cursorAfter.x&&cursorBefore.y==cursorAfter.y;
 std::ofstream saved(out/L"private-launch.json",std::ios::binary);saved<<launch.dump(2);std::cout<<launch.dump()<<"\n";return exitCode;
}
