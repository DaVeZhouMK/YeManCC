#pragma once
using json=Json;
#include "monitor_notice_log.h"
#include "webview_composition_host.h"
static std::vector<Json> noticeDiagnosticReceipts;
static void appendNativeLifecycleLog(const char* event,Json value){value["event"]=event;noticeDiagnosticReceipts.push_back(std::move(value));}
#include "notice-log-helper.inc"
static std::map<std::string,std::function<Json(const Json&)>> nativeHandlers;
static void ipc_on(const std::string& name,std::function<Json(const Json&)> handler){nativeHandlers[name]=std::move(handler);}
#include "notice-root-destroy-fixture.h"

namespace noticeRootDestroy {
static ComPtr<ICoreWebView2Controller> mainController;
static ::ymcc::webview::CompositionHost mainComposition;
static HRESULT mainCloseResult=E_PENDING;
static void closeWebViewsForExit(){
 ++closeViewsCalls;
 mainComposition.reset();
 if(mainController)mainCloseResult=mainController->Close();
 g_view.Reset();mainController.Reset();
}

template<class Check,class Admit>
static void runReal(Carrier& carrier,ICoreWebView2Environment* env,Check&& check,Admit&& admit,Json& report){
 registerClass();
 Route route{carrier,false};
 HWND root=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,L"YMCC_Notice_Root_Destroy_Test",L"Private root",WS_POPUP|WS_DISABLED,
  0,0,32,32,nullptr,nullptr,GetModuleHandleW(nullptr),&route);
 need(root!=nullptr,"Private root HWND was not created");++ownedRoots;
 struct Cleanup {
  HWND root;
  ~Cleanup(){
   g_mainWallpaperWitness.reset();
   if(mainController||mainComposition.active())closeWebViewsForExit();else mainComposition.reset();
   if(g_exitCleanupThread){CloseHandle(g_exitCleanupThread);g_exitCleanupThread=nullptr;}
   if(IsWindow(root)){SetWindowLongPtrW(root,GWLP_USERDATA,0);::DestroyWindow(root);}
   g_hwnd=nullptr;--ownedRoots;
  }
 } cleanup{root};
 g_hwnd=root;
 const HRESULT preflight=mainComposition.prepare(root,env);
 report["mainControllerPreflight"]={{"hr",static_cast<long>(preflight)},{"stage",mainComposition.failureStage()}};
 need(SUCCEEDED(preflight),"Private root composition preflight failed");
 ComPtr<ICoreWebView2Environment3> env3;
 need(SUCCEEDED(env->QueryInterface(IID_PPV_ARGS(&env3))),"Main composition environment unavailable");
 struct MainCreation {HRESULT hr=E_PENDING;bool done{};ComPtr<ICoreWebView2CompositionController> controller;};
 const auto creation=std::make_shared<MainCreation>();
 const HRESULT requested=env3->CreateCoreWebView2CompositionController(root,Callback<ICoreWebView2CreateCoreWebView2CompositionControllerCompletedHandler>(
  [creation](HRESULT hr,ICoreWebView2CompositionController* value)->HRESULT{creation->hr=hr;creation->controller=value;creation->done=true;return S_OK;}).Get());
 report["mainControllerRequestHr"]=static_cast<long>(requested);
 need(SUCCEEDED(requested),"Main composition controller creation request failed");
 const bool completed=until([&]{return creation->done;},12000);
 report["mainControllerCompletion"]={{"callbackArrived",completed},{"hr",static_cast<long>(creation->hr)},{"hasController",bool(creation->controller)}};
 need(completed&&SUCCEEDED(creation->hr)&&creation->controller,"Actual main composition controller was not created");
 auto composition=std::move(creation->controller);
 need(SUCCEEDED(composition.As(&mainController)),"Actual main controller unavailable");
 const HRESULT attached=mainComposition.attach(root,env,composition.Get());
 report["mainControllerAttach"]={{"hr",static_cast<long>(attached)},{"stage",mainComposition.failureStage()}};
 need(SUCCEEDED(attached),"Main composition controller attach failed");
 composition.Reset();env3.Reset();
 RECT bounds{};
 need(GetClientRect(root,&bounds)&&SUCCEEDED(mainController->put_Bounds(bounds)),"Main controller bounds failed");
 need(SUCCEEDED(mainController->get_CoreWebView2(&g_view))&&g_view&&admit(g_view.Get(),root),"Main view was not admitted to the owned Job and page mapping");
 const auto witness=std::make_shared<WallpaperWitness>();g_mainWallpaperWitness=witness;
 registerAppExit();registerMainMessageCallback();
 need(SUCCEEDED(g_view->Navigate(L"https://app.localhost/root-exit.html")),"Private root page navigation failed");
 need(until([&]{return !witness->echo.empty();},12000)&&witness->echo==Json::array({"ready"})&&!witness->wrongSender,"Main callback did not receive the owned page ready message");
 witness->echo.clear();

 const HWND notice=CarrierTestAccess::window(carrier);
 const auto noticeOwner=CarrierTestAccess::state(carrier);
 const auto active=[&]{return IsWindow(root)&&IsWindow(notice)&&g_view&&mainController&&!noticeOwner.expired()&&carrier.status().value("ready",false)&&!route.delivered&&!g_exitCleanupCompleted&&!closeViewsCalls;};
 g_trayActive=true;SendMessageW(root,WM_CLOSE,0,0);
 g_trayActive=false;g_taskbarResident=true;SendMessageW(root,WM_CLOSE,0,0);
 check("both resident close messages hide and preserve the live browser and notice",active()&&hides==2&&!g_exitRequested);
 g_exitCleanupThread=CreateEventW(nullptr,TRUE,TRUE,nullptr);
 need(g_exitCleanupThread!=nullptr,"Private cleanup event was not created");
 hostLocalOwnerReturned=true;
 SendMessageW(root,WM_APP_EXIT_READY,91,0);
 check("READY without exit intent preserves owners",active()&&!exitStarts&&!g_exitRequested);
 need(ResetEvent(g_exitCleanupThread)!=FALSE,"Private cleanup event could not be reset");

 // Keep the one real posted exit in hand while delivering the real browser reply.
 MSG exitMessage{};unsigned queued=0;
 const auto pumpReplies=[&]{
  const auto end=GetTickCount64()+8000;
  do{
   MSG message{};
   while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
    if(message.hwnd==root&&message.message==WM_APP_EXIT){++queued;exitMessage=message;continue;}
    need(message.message!=WM_QUIT,"Unexpected exit while WebView reply was pending");
    TranslateMessage(&message);DispatchMessageW(&message);
   }
   if(!witness->echo.empty())return;
   MsgWaitForMultipleObjects(0,nullptr,FALSE,5,QS_ALLINPUT);
  }while(GetTickCount64()<end);
  throw std::runtime_error("Private page did not confirm its native exchange");
 };
 const auto send=[&](LPCWSTR text){
  witness->echo.clear();
  need(SUCCEEDED(g_view->PostWebMessageAsJson(text)),"Private page stimulus could not be sent");
  pumpReplies();
 };
 g_permissions.emplace("app.*",false);
 send(LR"({"fixtureSend":"deny"})");
 const Json denied={{"id",801},{"error","permission denied: app.exit"}};
 check("real page receives the original permission error without an exit",witness->echo==Json::array({denied})&&ipcReplies.size()==1&&!queued&&active());
 g_permissions.clear();
 send(LR"({"fixtureSend":"rejected"})");
 check("bad shape and witness-consumed message queue no exit or reply",witness->echo==Json::array({"fence"})&&witness->seen.size()==3&&
  witness->seen[1]==42&&witness->seen[2].value("id",0)==804&&ipcReplies.size()==1&&!queued&&active());
 send(LR"({"fixtureSend":"allow"})");
 const Json accepted={{"id",803},{"result",true}};
 const DWORD ui=GetWindowThreadProcessId(root,nullptr);
 need(ipcReplies.size()==2,"Expected one permission reply and one success reply");
 const auto& reply=ipcReplies.back();
 check("real page receives the correlated success from the original handler on the root thread",witness->echo==Json::array({accepted})&&
  ipcReplies.size()==2&&reply.body==accepted&&reply.threadId==ui&&reply.childId==0&&reply.rootAlive&&!reply.exitRequested&&
  witness->seen.size()==4&&witness->seen[3].value("id",0)==803&&!witness->wrongSender&&active());
 check("actual page request posts one original exit message with the supplied code",queued==1&&exitMessage.wParam==73&&exitMessage.lParam==0&&!noticeDiagnosticReceipts.empty()&&
  noticeDiagnosticReceipts.back()==Json{{"event","app-exit-ipc"},{"code",73},{"threadId",ui},{"windowAlive",true}});
 report["rootBrowserExchange"]={{"receivedReplies",Json::array({denied,accepted})},{"observedRequests",witness->seen},{"queuedExitMessages",queued}};
 DispatchMessageW(&exitMessage);
 check("original exit message enters cleanup once without retiring the notice",active()&&g_exitRequested&&exitStarts==1&&exitTrigger=="wm-app-exit");
 const auto event=g_exitCleanupThread;
 const auto stillWaiting=[&]{g_exitReadyPosted=true;SendMessageW(root,WM_APP_EXIT_READY,73,0);return active()&&g_exitCleanupThread==event&&!g_exitReadyPosted;};
 check("unsignalled cleanup preserves both browser owners",stillWaiting());
 hostLocalOwnerReturned=false;need(SetEvent(event)!=FALSE,"Private event signal failed");
 check("pending local owner preserves both browser owners",stillWaiting());
 hostLocalOwnerReturned=true;
 SendMessageW(root,WM_APP_EXIT_READY,73,0);
 const bool rootDestroyed=!IsWindow(root),noticeDestroyed=!IsWindow(notice),noticeOwnerReleased=noticeOwner.expired(),
  carrierEmpty=CarrierTestAccess::empty(carrier),viewReleased=!g_view,controllerReleased=!mainController,
  compositionReleased=!mainComposition.active(),cleanupHandleReleased=!g_exitCleanupThread;
 const bool retiredAtReturn=rootDestroyed&&noticeDestroyed&&noticeOwnerReleased&&carrierEmpty&&route.delivered==1&&
  route.readyTeardownObserved&&SUCCEEDED(mainCloseResult)&&viewReleased&&controllerReleased&&compositionReleased&&
  cleanupHandleReleased&&unexpectedTrayCalls==0;
 // Keep synchronous retirement separate from consuming the quit in the UI loop.
 // Real WebView teardown can leave ordinary work pending on this private queue.
 MSG quit{};unsigned ordinaryMessages=0;DWORD queueWait=WAIT_OBJECT_0;bool sawQuit=false;
 const auto readStarted=GetTickCount64(),readDeadline=readStarted+2000;
 if(retiredAtReturn){
  for(auto now=GetTickCount64();now<readDeadline;now=GetTickCount64()){
   MSG message{};
   if(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
    if(message.message==WM_QUIT){quit=message;sawQuit=true;break;}
    ++ordinaryMessages;
    TranslateMessage(&message);DispatchMessageW(&message);
   }else{
    queueWait=MsgWaitForMultipleObjectsEx(0,nullptr,static_cast<DWORD>(readDeadline-now),QS_ALLINPUT,MWMO_INPUTAVAILABLE);
    if(queueWait==WAIT_FAILED||queueWait==WAIT_TIMEOUT)break;
   }
  }
 }
 report["rootReadyReturn"]={{"rootDestroyed",rootDestroyed},{"noticeDestroyed",noticeDestroyed},
  {"noticeOwnerReleased",noticeOwnerReleased},{"carrierEmpty",carrierEmpty},{"destroyDeliveries",route.delivered},
  {"readyTeardownObserved",route.readyTeardownObserved},{"mainCloseHr",static_cast<long>(mainCloseResult)},
  {"viewReleased",viewReleased},{"controllerReleased",controllerReleased},{"compositionReleased",compositionReleased},
  {"cleanupHandleReleased",cleanupHandleReleased},{"unexpectedTrayCalls",unexpectedTrayCalls},
  {"retiredAtReturn",retiredAtReturn},{"quitObserved",sawQuit},{"quitCode",sawQuit?Json(quit.wParam):Json(nullptr)},
  {"otherMessagesConsumed",ordinaryMessages},{"queueObservationMs",GetTickCount64()-readStarted},{"queueLastWait",queueWait},
  {"latchedExitCode",g_exitCode.load(std::memory_order_relaxed)},{"closeViewsCalls",closeViewsCalls},{"recoveryStops",recoveryStops}};
 check("READY releases the real main view and destroys root and notice before return",retiredAtReturn&&sawQuit&&quit.wParam==73);
}
}
