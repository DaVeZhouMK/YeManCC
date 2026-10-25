#define NOMINMAX
#define YMCC_RADIAL_NATIVE_TEST 1
#include "monitor_desktop_hud.h"
#include "WebView2EnvironmentOptions.h"
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace ymcc::desktophud;
using Microsoft::WRL::RuntimeClass;using Microsoft::WRL::RuntimeClassFlags;using Microsoft::WRL::ClassicCom;using Microsoft::WRL::ChainInterfaces;using Microsoft::WRL::Make;
namespace ymcc::desktophud {struct CarrierTestAccess {
 static HWND window(Carrier& c){return c.current?c.current->hwnd:nullptr;}
 static ICoreWebView2* view(Carrier& c){return c.current?c.current->view.Get():nullptr;}
 static std::weak_ptr<void> state(Carrier& c){return c.current;}
 static bool empty(Carrier& c){return !c.current;}
 static size_t hooks(){return Carrier::sharedHooks.size();}
 static size_t owners(){return Carrier::eventOwners.size();}
};}
static void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE))if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}}
template<class Predicate>static bool until(Predicate condition,DWORD timeout){const auto end=GetTickCount64()+timeout;do{pump();if(condition())return true;MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);}while(GetTickCount64()<end);pump();return condition();}
static std::wstring envValue(const wchar_t* name){const auto size=GetEnvironmentVariableW(name,nullptr,0);need(size>0,"Explicit reviewed runtime environment missing");std::wstring value(size,L'\0');GetEnvironmentVariableW(name,value.data(),size);value.resize(size-1);return value;}
struct Completion {
 HRESULT result=E_PENDING,afterForwardGetCore=E_PENDING,actualCloseResult=E_PENDING;unsigned actualCloseCalls=0,forwardingControllersDestroyed=0;bool held=false,forwarded=false,realController=false,closedAfterForward=false;
 ComPtr<ICoreWebView2Controller> controller;ComPtr<ICoreWebView2CompositionController> heldComposition;ComPtr<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler> heldHandler;
};
struct Probe {std::vector<std::shared_ptr<Completion>> completions;bool holdNext=false;unsigned started=0,completed=0;};
class ForwardController final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,ChainInterfaces<ICoreWebView2Controller3,ICoreWebView2Controller2,ICoreWebView2Controller>,ICoreWebView2CompositionController> {
 ComPtr<ICoreWebView2Controller3> controller;ComPtr<ICoreWebView2CompositionController> composition;std::shared_ptr<Completion> observed;
public:
 ForwardController(ICoreWebView2CompositionController* actual,std::shared_ptr<Completion> completion):composition(actual),observed(std::move(completion)){need(actual&&SUCCEEDED(actual->QueryInterface(IID_PPV_ARGS(&controller))),"Actual Controller3 unavailable");}
 ~ForwardController(){++observed->forwardingControllersDestroyed;}
 HRESULT STDMETHODCALLTYPE get_IsVisible(BOOL *isVisible)override{return controller->get_IsVisible(isVisible);}
 HRESULT STDMETHODCALLTYPE put_IsVisible(BOOL isVisible)override{return controller->put_IsVisible(isVisible);}
 HRESULT STDMETHODCALLTYPE get_Bounds(RECT *bounds)override{return controller->get_Bounds(bounds);}
 HRESULT STDMETHODCALLTYPE put_Bounds(RECT bounds)override{return controller->put_Bounds(bounds);}
 HRESULT STDMETHODCALLTYPE get_ZoomFactor(double *zoomFactor)override{return controller->get_ZoomFactor(zoomFactor);}
 HRESULT STDMETHODCALLTYPE put_ZoomFactor(double zoomFactor)override{return controller->put_ZoomFactor(zoomFactor);}
 HRESULT STDMETHODCALLTYPE add_ZoomFactorChanged(ICoreWebView2ZoomFactorChangedEventHandler *eventHandler, EventRegistrationToken *token)override{return controller->add_ZoomFactorChanged(eventHandler,token);}
 HRESULT STDMETHODCALLTYPE remove_ZoomFactorChanged(EventRegistrationToken token)override{return controller->remove_ZoomFactorChanged(token);}
 HRESULT STDMETHODCALLTYPE SetBoundsAndZoomFactor(RECT bounds, double zoomFactor)override{return controller->SetBoundsAndZoomFactor(bounds,zoomFactor);}
 HRESULT STDMETHODCALLTYPE MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON reason)override{return controller->MoveFocus(reason);}
 HRESULT STDMETHODCALLTYPE add_MoveFocusRequested(ICoreWebView2MoveFocusRequestedEventHandler *eventHandler, EventRegistrationToken *token)override{return controller->add_MoveFocusRequested(eventHandler,token);}
 HRESULT STDMETHODCALLTYPE remove_MoveFocusRequested(EventRegistrationToken token)override{return controller->remove_MoveFocusRequested(token);}
 HRESULT STDMETHODCALLTYPE add_GotFocus(ICoreWebView2FocusChangedEventHandler *eventHandler, EventRegistrationToken *token)override{return controller->add_GotFocus(eventHandler,token);}
 HRESULT STDMETHODCALLTYPE remove_GotFocus(EventRegistrationToken token)override{return controller->remove_GotFocus(token);}
 HRESULT STDMETHODCALLTYPE add_LostFocus(ICoreWebView2FocusChangedEventHandler *eventHandler, EventRegistrationToken *token)override{return controller->add_LostFocus(eventHandler,token);}
 HRESULT STDMETHODCALLTYPE remove_LostFocus(EventRegistrationToken token)override{return controller->remove_LostFocus(token);}
 HRESULT STDMETHODCALLTYPE add_AcceleratorKeyPressed(ICoreWebView2AcceleratorKeyPressedEventHandler *eventHandler, EventRegistrationToken *token)override{return controller->add_AcceleratorKeyPressed(eventHandler,token);}
 HRESULT STDMETHODCALLTYPE remove_AcceleratorKeyPressed(EventRegistrationToken token)override{return controller->remove_AcceleratorKeyPressed(token);}
 HRESULT STDMETHODCALLTYPE get_ParentWindow(HWND *parentWindow)override{return controller->get_ParentWindow(parentWindow);}
 HRESULT STDMETHODCALLTYPE put_ParentWindow(HWND parentWindow)override{return controller->put_ParentWindow(parentWindow);}
 HRESULT STDMETHODCALLTYPE NotifyParentWindowPositionChanged(void)override{return controller->NotifyParentWindowPositionChanged();}
 HRESULT STDMETHODCALLTYPE Close(void)override{++observed->actualCloseCalls;observed->actualCloseResult=controller->Close();return observed->actualCloseResult;}
 HRESULT STDMETHODCALLTYPE get_CoreWebView2(ICoreWebView2 **coreWebView2)override{return controller->get_CoreWebView2(coreWebView2);}
 HRESULT STDMETHODCALLTYPE get_DefaultBackgroundColor(COREWEBVIEW2_COLOR *value)override{return controller->get_DefaultBackgroundColor(value);}
 HRESULT STDMETHODCALLTYPE put_DefaultBackgroundColor(COREWEBVIEW2_COLOR value)override{return controller->put_DefaultBackgroundColor(value);}
 HRESULT STDMETHODCALLTYPE get_RasterizationScale(double *scale)override{return controller->get_RasterizationScale(scale);}
 HRESULT STDMETHODCALLTYPE put_RasterizationScale(double scale)override{return controller->put_RasterizationScale(scale);}
 HRESULT STDMETHODCALLTYPE get_ShouldDetectMonitorScaleChanges(BOOL *value)override{return controller->get_ShouldDetectMonitorScaleChanges(value);}
 HRESULT STDMETHODCALLTYPE put_ShouldDetectMonitorScaleChanges(BOOL value)override{return controller->put_ShouldDetectMonitorScaleChanges(value);}
 HRESULT STDMETHODCALLTYPE add_RasterizationScaleChanged(ICoreWebView2RasterizationScaleChangedEventHandler *eventHandler, EventRegistrationToken *token)override{return controller->add_RasterizationScaleChanged(eventHandler,token);}
 HRESULT STDMETHODCALLTYPE remove_RasterizationScaleChanged(EventRegistrationToken token)override{return controller->remove_RasterizationScaleChanged(token);}
 HRESULT STDMETHODCALLTYPE get_BoundsMode(COREWEBVIEW2_BOUNDS_MODE *boundsMode)override{return controller->get_BoundsMode(boundsMode);}
 HRESULT STDMETHODCALLTYPE put_BoundsMode(COREWEBVIEW2_BOUNDS_MODE boundsMode)override{return controller->put_BoundsMode(boundsMode);}
 HRESULT STDMETHODCALLTYPE get_RootVisualTarget(IUnknown **target)override{return composition->get_RootVisualTarget(target);}
 HRESULT STDMETHODCALLTYPE put_RootVisualTarget(IUnknown *target)override{return composition->put_RootVisualTarget(target);}
 HRESULT STDMETHODCALLTYPE SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND eventKind, COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS virtualKeys, UINT32 mouseData, POINT point)override{return composition->SendMouseInput(eventKind,virtualKeys,mouseData,point);}
 HRESULT STDMETHODCALLTYPE SendPointerInput(COREWEBVIEW2_POINTER_EVENT_KIND eventKind, ICoreWebView2PointerInfo *pointerInfo)override{return composition->SendPointerInput(eventKind,pointerInfo);}
 HRESULT STDMETHODCALLTYPE get_Cursor(HCURSOR *cursor)override{return composition->get_Cursor(cursor);}
 HRESULT STDMETHODCALLTYPE get_SystemCursorId(UINT32 *systemCursorId)override{return composition->get_SystemCursorId(systemCursorId);}
 HRESULT STDMETHODCALLTYPE add_CursorChanged(ICoreWebView2CursorChangedEventHandler *eventHandler, EventRegistrationToken *token)override{return composition->add_CursorChanged(eventHandler,token);}
 HRESULT STDMETHODCALLTYPE remove_CursorChanged(EventRegistrationToken token)override{return composition->remove_CursorChanged(token);}
};
class ForwardEnvironment final:public RuntimeClass<RuntimeClassFlags<ClassicCom>,ChainInterfaces<ICoreWebView2Environment3,ICoreWebView2Environment2,ICoreWebView2Environment>> {
 ComPtr<ICoreWebView2Environment3> real;std::shared_ptr<Probe> probe;
public:
 ForwardEnvironment(ICoreWebView2Environment* actual,std::shared_ptr<Probe> observed):probe(std::move(observed)){need(SUCCEEDED(actual->QueryInterface(IID_PPV_ARGS(&real))),"Actual Environment3 unavailable");}
 HRESULT STDMETHODCALLTYPE CreateCoreWebView2Controller(HWND parent,ICoreWebView2CreateCoreWebView2ControllerCompletedHandler* handler)override{return real->CreateCoreWebView2Controller(parent,handler);}
 HRESULT STDMETHODCALLTYPE CreateWebResourceResponse(IStream* content,int code,LPCWSTR phrase,LPCWSTR headers,ICoreWebView2WebResourceResponse** response)override{return real->CreateWebResourceResponse(content,code,phrase,headers,response);}
 HRESULT STDMETHODCALLTYPE get_BrowserVersionString(LPWSTR* version)override{return real->get_BrowserVersionString(version);}
 HRESULT STDMETHODCALLTYPE add_NewBrowserVersionAvailable(ICoreWebView2NewBrowserVersionAvailableEventHandler* handler,EventRegistrationToken* token)override{return real->add_NewBrowserVersionAvailable(handler,token);}
 HRESULT STDMETHODCALLTYPE remove_NewBrowserVersionAvailable(EventRegistrationToken token)override{return real->remove_NewBrowserVersionAvailable(token);}
 HRESULT STDMETHODCALLTYPE CreateWebResourceRequest(LPCWSTR uri,LPCWSTR method,IStream* post,LPCWSTR headers,ICoreWebView2WebResourceRequest** request)override{return real->CreateWebResourceRequest(uri,method,post,headers,request);}
 HRESULT STDMETHODCALLTYPE CreateCoreWebView2PointerInfo(ICoreWebView2PointerInfo** info)override{return real->CreateCoreWebView2PointerInfo(info);}
 HRESULT STDMETHODCALLTYPE CreateCoreWebView2CompositionController(HWND parent,ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler* handler)override{
  auto completion=std::make_shared<Completion>();completion->held=probe->holdNext;probe->holdNext=false;probe->completions.push_back(completion);++probe->started;
  ComPtr<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler> original=handler;auto observed=probe;
  return real->CreateCoreWebView2CompositionController(parent,Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>([completion,original,observed](HRESULT hr,ICoreWebView2CompositionController* composition)->HRESULT{
   completion->result=hr;completion->realController=composition&&SUCCEEDED(composition->QueryInterface(IID_PPV_ARGS(&completion->controller)));++observed->completed;
   ComPtr<ForwardController> forwardingController;if(composition)forwardingController=Make<ForwardController>(composition,completion);
   if(completion->held){completion->heldComposition=forwardingController.Get();completion->heldHandler=original;return S_OK;}
   const auto result=original->Invoke(hr,forwardingController.Get());completion->forwarded=true;ComPtr<ICoreWebView2> view;if(completion->controller){completion->afterForwardGetCore=completion->controller->get_CoreWebView2(&view);completion->closedAfterForward=completion->actualCloseCalls>0&&SUCCEEDED(completion->actualCloseResult);}return result;
  }).Get());
 }
};
static void releaseHeld(const std::shared_ptr<Completion>& completion){need(completion->heldHandler&&completion->realController&&SUCCEEDED(completion->result),"Actual held controller was not created");need(SUCCEEDED(completion->heldHandler->Invoke(completion->result,completion->heldComposition.Get())),"Actual production completion failed");completion->forwarded=true;ComPtr<ICoreWebView2> view;completion->afterForwardGetCore=completion->controller->get_CoreWebView2(&view);completion->closedAfterForward=completion->actualCloseCalls>0&&SUCCEEDED(completion->actualCloseResult);completion->heldHandler.Reset();completion->heldComposition.Reset();}
static bool closed(const std::shared_ptr<Completion>& completion){return completion->actualCloseCalls>0&&SUCCEEDED(completion->actualCloseResult);}
static std::wstring lower(std::wstring value){for(auto& c:value)c=static_cast<wchar_t>(towlower(c));return value;}
static int worker(const std::filesystem::path& web,const std::filesystem::path& out){
 Json report={{"status","FAIL"},{"cases",Json::array()},{"actualWebView2Controllers",true},{"controllerTestDouble",false},{"environmentForwardingObserver",true},{"syntheticLocalRendererHandshake",true},{"actualProductRendererVue",false},{"privateDesktop",true},{"desktopSwitched",false},{"normalYmccStarted",false},{"deviceIo",false},{"sleep",false},{"manualSleepAcceptanceReady",false}};
 bool initialized=false;HANDLE browser=nullptr;ComPtr<ICoreWebView2Environment> realEnvironment;ComPtr<ForwardEnvironment> forwarding;auto probe=std::make_shared<Probe>();unsigned checks=0;std::string error;Json events=Json::array();
 auto check=[&](const char* name,bool value){++checks;report["cases"].push_back({{"name",name},{"passed",value}});need(value,name);};
 try{
  const auto runtime=envValue(L"YMCC_RADIAL_FIXED_RUNTIME"),version=envValue(L"YMCC_RADIAL_FIXED_RUNTIME_VERSION");LPWSTR actual=nullptr;need(SUCCEEDED(GetAvailableCoreWebView2BrowserVersionString(runtime.c_str(),&actual))&&actual,"Explicit runtime version unavailable");const bool exact=version==actual;report["runtimeVersion"]=utf8(actual);CoTaskMemFree(actual);need(exact,"Explicit runtime version mismatch");
  initialized=SUCCEEDED(CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED));need(initialized,"STA COM initialization failed");
  auto options=Make<CoreWebView2EnvironmentOptions>();options->put_AllowSingleSignOnUsingOSPrimaryAccount(FALSE);options->put_AdditionalBrowserArguments(L"--disable-background-networking --disable-component-update --disable-sync --no-first-run --disable-features=msSmartScreenProtection,WebRtcAllowInputVolumeAdjustment --host-resolver-rules=MAP * ~NOTFOUND --autoplay-policy=user-gesture-required");
  bool complete=false;HRESULT environmentResult=E_PENDING;const auto profile=out/L"owned-webview-profile";const auto requested=CreateCoreWebView2EnvironmentWithOptions(runtime.c_str(),profile.c_str(),options.Get(),Callback<ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler>([&](HRESULT hr,ICoreWebView2Environment* value)->HRESULT{environmentResult=hr;realEnvironment=value;complete=true;return S_OK;}).Get());
  need(SUCCEEDED(requested)&&until([&]{return complete;},20000)&&SUCCEEDED(environmentResult)&&realEnvironment,"Actual explicit WebView2 environment creation failed");forwarding=Make<ForwardEnvironment>(realEnvironment.Get(),probe);
  auto configure=[&](Carrier& carrier){Config config;config.noticeOnly=true;config.serve=[&](ICoreWebView2* view){ComPtr<ICoreWebView2_3> resource;return SUCCEEDED(view->QueryInterface(IID_PPV_ARGS(&resource)))&&SUCCEEDED(resource->SetVirtualHostNameToFolderMapping(L"app.localhost",web.c_str(),COREWEBVIEW2_HOST_RESOURCE_ACCESS_KIND_DENY_CORS));};config.notify=[&](const std::string& event,const Json& value){events.push_back({{"event",event},{"data",value}});};carrier.initialize(forwarding.Get(),std::move(config));};
  auto create=[&](Carrier& carrier,const std::string& nonce){auto lease=carrier.reserveNotice(nonce)["leaseId"].get<unsigned long long>();auto result=carrier.create(0,0,nonce,true,lease);result["leaseId"]=lease;return result;};
  auto ready=[&](Carrier& carrier){return until([&]{return carrier.status().value("ready",false);},12000);};
  auto retire=[&](Carrier& carrier,const Json& owner){const auto result=carrier.retireNotice(owner["leaseId"].get<unsigned long long>(),owner["nonce"].get<std::string>());return result.value("leaseRetired",false)&&result.value("resourcesDestroyed",false)&&result.value("ownedOnly",false);};
  {
   Carrier carrier;configure(carrier);auto owner=create(carrier,"actual-webview-ready-owner-0001");check("actual composition controller and inert local renderer become ready",ready(carrier));auto completion=probe->completions.back();need(completion->realController,"No actual composition controller");UINT32 pid=0;need(SUCCEEDED(CarrierTestAccess::view(carrier)->get_BrowserProcessId(&pid))&&pid,"Actual browser PID unavailable");browser=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);need(browser,"Own browser observation handle unavailable");wchar_t image[32768]{};DWORD length=32768;need(QueryFullProcessImageNameW(browser,0,image,&length)&&lower(image)==lower((std::filesystem::path(runtime)/L"msedgewebview2.exe").wstring()),"Browser image differs from explicit runtime");FILETIME birth{},end{},kernel{},user{},workerBirth{};need(GetProcessTimes(browser,&birth,&end,&kernel,&user)&&GetProcessTimes(GetCurrentProcess(),&workerBirth,&end,&kernel,&user),"Own process birth unavailable");need(CompareFileTime(&birth,&workerBirth)>=0,"Browser predates own worker");report["browserPid"]=pid;report["browserObservedImage"]=utf8(image);report["browserBirth"]=((ULONGLONG)birth.dwHighDateTime<<32)|birth.dwLowDateTime;
   HWND window=CarrierTestAccess::window(carrier);auto weak=CarrierTestAccess::state(carrier);check("native exact retirement destroys actual WebView2 parent HWND",retire(carrier,owner)&&!IsWindow(window));check("production retirement invokes actual controller Close and receives success",closed(completion));check("actual renderer callbacks do not retain the retired native State",weak.expired()&&CarrierTestAccess::empty(carrier));carrier.shutdown();completion->controller.Reset();
  }
  {
   Carrier carrier;configure(carrier);auto owner=create(carrier,"actual-webview-normal-close-0002");need(ready(carrier),"Ordinary close fixture not ready");auto completion=probe->completions.back();const auto id=owner["id"].get<int>();check("ordinary production close closes real controller and owns no State",carrier.close(id,owner["nonce"].get<std::string>())&&closed(completion)&&CarrierTestAccess::empty(carrier));bool rejected=false;try{carrier.create(0,0,owner["nonce"].get<std::string>(),true,owner["leaseId"].get<unsigned long long>());}catch(const std::exception& e){rejected=std::string(e.what()).find("lease retired")!=std::string::npos;}check("ordinary close revokes old native create lease",rejected);carrier.shutdown();completion->controller.Reset();
  }
  {
   Carrier carrier;configure(carrier);probe->holdNext=true;auto owner=create(carrier,"actual-webview-late-retire-0003");auto completion=probe->completions.back();need(until([&]{return completion->realController&&completion->heldHandler;},12000),"Actual late controller creation did not complete");const auto weak=CarrierTestAccess::state(carrier);check("real completed controller is held before delivery to production",!completion->forwarded&&!weak.expired());check("retire releases native State before real completion callback arrives",retire(carrier,owner)&&weak.expired());releaseHeld(completion);check("late actual completion closes its real controller and cannot recreate State",completion->closedAfterForward&&CarrierTestAccess::empty(carrier));carrier.shutdown();completion->controller.Reset();
  }
  {
   auto carrier=std::make_unique<Carrier>();configure(*carrier);probe->holdNext=true;auto owner=create(*carrier,"actual-webview-after-destructor-0004");auto completion=probe->completions.back();need(until([&]{return completion->realController&&completion->heldHandler;},12000),"Destructor fixture did not create real controller");HWND window=CarrierTestAccess::window(*carrier);auto weak=CarrierTestAccess::state(*carrier);carrier.reset();check("Carrier destructor destroys HWND and expires State while actual callback is held",!IsWindow(window)&&weak.expired());releaseHeld(completion);check("actual completion after Carrier destructor is safe and closes controller",completion->closedAfterForward);completion->controller.Reset();
  }
  {
   Carrier carrier;configure(carrier);probe->holdNext=true;auto old=create(carrier,"actual-webview-old-owner-0005");auto oldCompletion=probe->completions.back();need(until([&]{return oldCompletion->realController&&oldCompletion->heldHandler;},12000),"Old real callback not held");need(retire(carrier,old),"Old native lease not retired");auto current=create(carrier,"actual-webview-current-owner-0006");need(ready(carrier),"New actual owner did not become ready");auto currentCompletion=probe->completions.back();const auto currentWindow=CarrierTestAccess::window(carrier);releaseHeld(oldCompletion);check("late real old callback closes only old controller and preserves new owner",oldCompletion->closedAfterForward&&!closed(currentCompletion)&&CarrierTestAccess::window(carrier)==currentWindow&&carrier.status().value("ready",false));check("new current actual controller retires independently",retire(carrier,current)&&closed(currentCompletion));carrier.shutdown();oldCompletion->controller.Reset();currentCompletion->controller.Reset();
  }
  for(unsigned i=0;i<3;i++){Carrier carrier;configure(carrier);const auto nonce="actual-webview-repeat-owner-000"+std::to_string(i);auto owner=create(carrier,nonce);need(ready(carrier),"Repeated actual owner not ready");auto completion=probe->completions.back();auto weak=CarrierTestAccess::state(carrier);check(("repeat actual controller lifecycle "+std::to_string(i)).c_str(),retire(carrier,owner)&&weak.expired()&&closed(completion));carrier.shutdown();completion->controller.Reset();}
  check("all nine real completion callbacks forwarded and references released",probe->started==9&&probe->completed==9&&std::all_of(probe->completions.begin(),probe->completions.end(),[](const auto& c){return c->realController&&c->forwarded&&c->actualCloseCalls==1&&SUCCEEDED(c->actualCloseResult)&&c->forwardingControllersDestroyed==1&&!c->controller&&!c->heldComposition&&!c->heldHandler;}));check("all production native event hooks and owners released",CarrierTestAccess::hooks()==0&&CarrierTestAccess::owners()==0);
  forwarding.Reset();realEnvironment.Reset();check("own explicit browser exits naturally before owned Job cleanup",browser&&until([&]{return WaitForSingleObject(browser,0)==WAIT_OBJECT_0;},15000));report["browserNaturalExitBeforeJobClose"]=true;report["status"]="PASS";
 }catch(const std::exception& failure){error=failure.what();report["error"]=error;report["win32Error"]=GetLastError();}
 for(auto& c:probe->completions){if(c->controller)c->controller->Close();c->heldHandler.Reset();c->heldComposition.Reset();c->controller.Reset();}forwarding.Reset();realEnvironment.Reset();if(browser)CloseHandle(browser);if(initialized)CoUninitialize();report["startedActualCompositionControllers"]=probe->started;report["completedActualCompositionControllers"]=probe->completed;report["actualControllerCloseObservations"]=Json::array();for(const auto& c:probe->completions)report["actualControllerCloseObservations"].push_back({{"realController",c->realController},{"actualCloseCalls",c->actualCloseCalls},{"actualCloseResult",(long)c->actualCloseResult},{"forwardingControllersDestroyed",c->forwardingControllersDestroyed},{"postCloseGetterResult",(long)c->afterForwardGetCore}});report["checks"]=checks;report["events"]=events;report["remainingNativeHooks"]=CarrierTestAccess::hooks();report["remainingNativeOwners"]=CarrierTestAccess::owners();std::ofstream saved(out/L"real-webview-result.json",std::ios::binary);saved<<report.dump(2)<<'\n';std::cout<<Json{{"status",report["status"]},{"checks",checks},{"actualControllers",probe->completed},{"error",error}}.dump()<<'\n';return report["status"]=="PASS"&&saved.good()?0:2;
}
static std::wstring quoted(const std::wstring& value){need(value.find(L'"')==std::wstring::npos,"Quoted path rejected");return L"\""+value+L"\"";}
int wmain(int argc,wchar_t** argv){
 if(argc==4&&std::wstring(argv[1])==L"--owned-worker")return worker(argv[2],argv[3]);if(argc!=4)return 10;const std::filesystem::path web=argv[1],out=argv[2];HDESK desktop=nullptr;HANDLE job=nullptr;PROCESS_INFORMATION child{};Json report={{"status","FAIL"},{"privateDesktop",true},{"desktopSwitched",false},{"ownedJobOnly",true},{"normalYmccStarted",false},{"forcedTermination",false}};int exit=2;
 try{const auto name=L"YMCC_Notice_Real_WebView_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);need(desktop,"Private desktop creation failed");job=CreateJobObjectW(nullptr,nullptr);need(job,"Own Job unavailable");JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;need(SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)),"Own Job cleanup policy failed");wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);auto command=quoted(executable)+L" --owned-worker "+quoted(web.wstring())+L" "+quoted(out.wstring());std::wstring desktopName=name;STARTUPINFOW startup{sizeof(startup)};startup.lpDesktop=desktopName.data();need(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&startup,&child),"Own private worker creation failed");if(!AssignProcessToJobObject(job,child.hProcess)){TerminateProcess(child.hProcess,3);throw std::runtime_error("Own worker Job binding failed");}ResumeThread(child.hThread);report["workerPid"]=child.dwProcessId;report["desktopName"]=utf8(name);const auto waited=WaitForSingleObject(child.hProcess,120000);if(waited!=WAIT_OBJECT_0){report["forcedTermination"]=true;TerminateJobObject(job,3);throw std::runtime_error("Own worker original120s deadline exceeded");}DWORD code=2;need(GetExitCodeProcess(child.hProcess,&code),"Own worker exit unavailable");report["workerExitCode"]=code;exit=static_cast<int>(code);report["status"]=code==0?"PASS":"FAIL";
 }catch(const std::exception& e){report["error"]=e.what();report["win32Error"]=GetLastError();}if(job)CloseHandle(job);if(child.hThread)CloseHandle(child.hThread);if(child.hProcess)CloseHandle(child.hProcess);if(desktop)report["privateDesktopClosed"]=CloseDesktop(desktop)!=FALSE;std::ofstream saved(out/L"real-webview-private-launch.json",std::ios::binary);saved<<report.dump(2)<<'\n';std::cout<<report.dump()<<'\n';return exit;
}
