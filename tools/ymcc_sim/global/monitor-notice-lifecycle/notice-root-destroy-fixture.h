#pragma once
#include <atomic>
#include <mutex>
#include <shellapi.h>
#ifdef YMCC_TEST_ROOT_EXIT_INTENT
#include <exception>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#endif

// The exact root case is compiled below. Only the notice Carrier and owned
// Win32 windows/timers run; every neighboring owner has a fixture substitute.
namespace noticeRootDestroy {
static std::atomic<bool> g_exitRequested{},g_ccdStop{};
static std::atomic<int> g_exitCode{73};
static std::atomic<HWND> g_wakeGameRecoveryStatusWindow{};
static std::mutex g_gameCorePolicyMutex;
static HWND g_rawInputSinkHwnd{};
static HANDLE g_suspendResumeNotify{},g_autoCloseThread{},g_tdpDaemonJob{};
static HPOWERNOTIFY g_acdcNotify{},g_monitorNotify{},g_idleDisplayNotify{},g_schemeNotify{};
static HICON g_memTrayIcon{};
static bool g_trayActive{};
static NOTIFYICONDATAW g_nid{};
static std::map<int,int*> g_watchers;
static unsigned inputFences=0,ccdStops=0,poolStops=0,rootDestroyDeliveries=0,ownedRoots=0,unexpectedTrayCalls=0;
static BOOL rejectTrayCall(DWORD,PNOTIFYICONDATAW){++unexpectedTrayCalls;return FALSE;}
struct InertOwner {
 void retire(){} void shutdown(){} void requestExitFence(){} void stop(){} void requestStop(){}
 void listen(std::nullptr_t){} void observe(std::nullptr_t){}
};
static InertOwner inert;
static InertOwner g_deckySidebarRuntime,g_fxSoundRecovery,g_gestureSummon,
 g_radialRawTouchObserver,g_radialTouchObserver,g_radialOverlay,g_desktopMonitorHud,g_steamSettingsObserver,
 g_steamModeObserver,g_steamCssGuard,g_steamCssUse,g_autoCloseWait;
static InertOwner *g_gamepadSerialEventMailbox=&inert,*g_steamTrackerCompletionDispatcher=&inert,*g_ipcResultMailbox=&inert;
struct GameSteamContext { static void cancelCompletionNotice(){} };
namespace ymcc {
 namespace batterycharge { static void seal(){} }
 namespace lossless { static InertOwner& runtimeMonitor(){return inert;} }
 namespace powerSource { static InertOwner& owner(){return inert;} }
}
static void steamControllerCursorShutdown(){} static void inputCaptureBeginExitOwner(int,const char*){++inputFences;}
static void nativeAyaLightingSessionStop(HWND){} static void cpuUvTrialStop(const char*){}
static void inputEdgeReconcileOnUi(bool){} static void closeScreenTouchpadsForExit(const char*){}
static void mousePolicyStop(){} static void msiNativeForegroundStopObserver(){}
static void steamOriginalRecoveryRequestStop(){} static void steamGameLinkShutdown(){}
static void steamSourceFactoryShutdown(){} static void rogEnsureXboxFaceEnabled(const char*){}
static void inputCaptureRequestStop(){} static bool gameCoreRestoreLeaseLocked(){return true;}
static void ccdShutdown(){++ccdStops;} static void poolStop(int){++poolStops;}
static void sgStopKernelPowerSubscription(){} static void sgStopWorkThread(int){} static void sgStopSleepRetry(){}
static ULONG PowerUnregisterSuspendResumeNotification(HANDLE){return 0;}
static void stopPowerSourceRecovery(HWND){} static void focusClearSession(){} static void stopTaskbarLayoutObserver(){}
static void stopPowerResumeWatchdog(){} static void stopTdpDaemonForExit(){} static void stopTopMonitorForExit(int){}
static bool sgCleanupBeforeExit(bool){return true;}
static unsigned closeViewsCalls=0;
#ifdef YMCC_TEST_REAL_ROOT_EXIT
static void closeWebViewsForExit();
#else
static void closeWebViewsForExit(){++closeViewsCalls;}
#endif
static bool stopWatcher(int*,int){return true;}
enum : UINT_PTR {TIMER_ID_ACDC=1,SUMMON_FOCUS_TIMER_ID,RETURN_GAME_FOCUS_TIMER_ID,RESUME_GAME_FOCUS_TIMER_ID,
 WAKE_FOCUS_NUDGE_TIMER_ID,FOCUS_DISPLAY_TIMER_ID,MEM_TRAY_TIMER_ID,GP_HOLD_TIMER_ID};

#define Shell_NotifyIconW ::noticeRootDestroy::rejectTrayCall
static LRESULT positiveCase(HWND h,UINT m,Carrier& g_monitorNoticeCarrier){
 switch(m){
#include "notice-root-destroy-case.inc"
 }
 return 1;
}
static LRESULT omissionCase(HWND h,UINT m,Carrier& g_monitorNoticeCarrier){
 switch(m){
#include "notice-root-destroy-omitted.inc"
 }
 return 1;
}
#undef Shell_NotifyIconW
#ifdef YMCC_TEST_ROOT_EXIT_INTENT
// The exit worker is substituted; an owned Win32 event supplies the kernel wait state.
static HANDLE g_exitCleanupThread{};
static std::atomic<bool> g_exitReadyPosted{},g_gpStateFallbackTimerArmed{};
static bool g_taskbarResident{},g_exitCleanupCompleted{},g_monitorEntryFailureTimerArmed{},g_gpHoldTimerOn{};
static ULONGLONG g_monitorEntryFailureGeneration{};
static bool hostLocalOwnerReturned{};
static std::atomic<const char*> g_exitCleanupStage{"fixture-exit-worker"};
static const char* g_exitLastWaitingStage{};
static ULONGLONG g_exitBeginTick{};
static const char* g_exitSessionId="private-notice-exit-intent";
static unsigned exitStarts{},hides{},recoveryStops{};
static std::string_view exitTrigger;
static Json intentStages=Json::array();
static Json appExitIpc=Json::object();
static HWND g_hwnd{};
enum : UINT_PTR {EXIT_CLEANUP_WATCHDOG_TIMER_ID=30,SG_RETRY_TIMER_ID,SG_MONITOR_ENTRY_FAILURE_TIMER_ID,
 GP_STATE_FALLBACK_TIMER_ID,WEBVIEW_RECOVERY_TIMER_ID,WEBVIEW_RECOVERY_ACTION_TIMER_ID};
static bool steamOriginalRecoveryStopped(){return true;}
static bool inputCaptureExitOwnershipReleased(){return true;}
static bool inputHostExitLocalOwnershipReleased(){return hostLocalOwnerReturned;}
static bool gamepadSerialExitOwnershipReleased(){return true;}
static void stopRecoveryService(){++recoveryStops;}
static void saveWindowState(){}
static void ipc_emit(const char*){}
static void hideWindowAnimated(HWND){++hides;}
static void beginAsyncExit(HWND,WPARAM code,const char* trigger){++exitStarts;exitTrigger=trigger;g_exitCode=static_cast<int>(code);g_exitBeginTick=GetTickCount64();g_exitRequested=true;}
static LRESULT intentCase(HWND h,UINT m,WPARAM w){
 switch(m){
#include "notice-root-intent-cases.inc"
 }
 return 1;
}
static void registerAppExit(){
#include "notice-root-exit-ipc.inc"
}
using json=Json;
#ifdef YMCC_TEST_REAL_ROOT_EXIT
static ComPtr<ICoreWebView2> g_view;
#else
struct PrivateViewToken { PrivateViewToken* Get(){return this;} };
using ICoreWebView2=PrivateViewToken;
static ICoreWebView2 g_view;
#endif
static int ipc_child_id_for_view(ICoreWebView2* view){return view==g_view.Get()?0:-1;}
static const auto& g_cmds=nativeHandlers;
static std::unordered_map<std::string,bool> g_permissions;
static std::atomic<unsigned long long> g_webviewGeneration{1};
static std::atomic<bool> g_traceOn{};
static void traceLog(const char*,...){}
static int g_asyncMode=2;
enum class TestFamily { Generic };
static struct { TestFamily family{TestFamily::Generic}; } g_machineIdentity;
static const char* ymccFamilyName(TestFamily){return "generic";}
static bool unexpectedAsyncDispatch{};
struct IpcReply {Json body;int childId;DWORD threadId;bool rootAlive;bool exitRequested;};
static std::vector<IpcReply> ipcReplies;
#ifdef YMCC_TEST_REAL_ROOT_EXIT
static bool ipc_post_json(int childId,const Json& message){
 ipcReplies.push_back({message,childId,GetCurrentThreadId(),IsWindow(g_hwnd)!=FALSE,g_exitRequested.load()});
 const auto payload=message.dump();
 const std::wstring wide(payload.begin(),payload.end());
 return childId==0&&g_view&&SUCCEEDED(g_view->PostWebMessageAsJson(wide.c_str()));
}
#else
static bool ipc_post_json(int childId,const Json& message){
 ipcReplies.push_back({message,childId,GetCurrentThreadId(),IsWindow(g_hwnd)!=FALSE,g_exitRequested.load()});
 return true;
}
#endif
// This projection substitutes the WebView identity and outbound transport, not the request logic.
#include "notice-root-exit-receiver.inc"
#ifdef YMCC_TEST_REAL_ROOT_EXIT
struct WallpaperWitness {
 Json seen=Json::array(),echo=Json::array();
 bool wrongSender{};
 bool message(ICoreWebView2* sender,ICoreWebView2WebMessageReceivedEventArgs*,LPCWSTR raw){
  if(sender!=g_view.Get()){wrongSender=true;return true;}
  const auto message=Json::parse(W2U(raw),nullptr,false);
  if(message.is_object()&&message.contains("fixtureReply")){echo.push_back(message["fixtureReply"]);return true;}
  seen.push_back(message);
  return message.is_object()&&message.value("id",0)==804;
 }
};
#else
struct ICoreWebView2WebMessageReceivedEventArgs {
 const std::wstring_view payload;
 unsigned reads{};
 HRESULT get_WebMessageAsJson(LPWSTR* out){
  ++reads;
  *out=static_cast<LPWSTR>(CoTaskMemAlloc((payload.size()+1)*sizeof(wchar_t)));
  if(!*out)return E_OUTOFMEMORY;
  std::copy(payload.begin(),payload.end(),*out);
  (*out)[payload.size()]=L'\0';
  return S_OK;
 }
};
struct WallpaperWitness {
 bool consume{true};
 bool matched{};
 unsigned consumed{},passed{};
 bool message(ICoreWebView2* sender,ICoreWebView2WebMessageReceivedEventArgs* args,LPCWSTR raw){
  matched=sender==g_view.Get()&&args->reads==1&&raw&&args->payload==raw;
  if(consume)++consumed;else ++passed;
  return consume;
 }
};
#endif
static std::shared_ptr<WallpaperWitness> g_mainWallpaperWitness;
static void closeMainWallpaperWitness(){g_mainWallpaperWitness.reset();}
#ifdef YMCC_TEST_REAL_ROOT_EXIT
static void registerMainMessageCallback(){
#include "notice-root-web-registration.inc"
}
#else
#include "notice-root-web-message.inc"
#endif
#endif
struct Route {Carrier& carrier;bool omit;unsigned delivered{};bool readyTeardownObserved{};};
static LRESULT CALLBACK dispatchProc(HWND h,UINT m,WPARAM w,LPARAM l){
 if(m==WM_NCCREATE){
  auto* create=reinterpret_cast<const CREATESTRUCTW*>(l);
  SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(create->lpCreateParams));
 }else if(m==WM_DESTROY){
  auto* route=reinterpret_cast<Route*>(GetWindowLongPtrW(h,GWLP_USERDATA));
  if(route){
   ++route->delivered;++rootDestroyDeliveries;
#ifdef YMCC_TEST_ROOT_EXIT_INTENT
   route->readyTeardownObserved=g_exitCleanupCompleted&&closeViewsCalls==1&&recoveryStops==1;
#endif
   return route->omit?omissionCase(h,m,route->carrier):positiveCase(h,m,route->carrier);
  }
 }
#ifdef YMCC_TEST_ROOT_EXIT_INTENT
 if(m==WM_CLOSE||m==WM_APP_EXIT||m==WM_APP_EXIT_READY)return intentCase(h,m,w);
#endif
 return DefWindowProcW(h,m,w,l);
}
static void registerClass(){
 WNDCLASSW cls{};cls.lpfnWndProc=dispatchProc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC_Notice_Root_Destroy_Test";
 need(RegisterClassW(&cls)!=0,"Private root class was not registered");
}
#if defined(YMCC_TEST_ROOT_EXIT_INTENT) && !defined(YMCC_TEST_REAL_ROOT_EXIT)
static void driveIntent(HWND root,const Route& route,const std::pair<HWND,HWND>& windows,Carrier& carrier){
 const auto closes=controllerClosed,destroyed=probe::destroyed;
 const auto requireActive=[&]{
  MSG quit{};
  need(IsWindow(root)&&route.delivered==0&&IsWindow(windows.first)&&IsWindow(windows.second)&&controllerLive==1&&
   controllerClosed==closes&&probe::destroyed==destroyed&&CarrierTestAccess::retained(carrier)&&!CarrierTestAccess::retired(carrier)&&
   !g_exitCleanupCompleted&&closeViewsCalls==0&&recoveryStops==0&&!PeekMessageW(&quit,nullptr,WM_QUIT,WM_QUIT,PM_NOREMOVE),
   "Premature root or notice teardown");
 };
 const auto requireHide=[&](unsigned expected){
  requireActive();need(hides==expected&&exitStarts==0&&!g_exitRequested,"Resident WM_CLOSE did not remain hide-only");
 };
 g_trayActive=true;
 SendMessageW(root,WM_CLOSE,0,0);requireHide(1);intentStages.push_back("tray-hide");
 g_trayActive=false;g_taskbarResident=true;
 SendMessageW(root,WM_CLOSE,0,0);requireHide(2);intentStages.push_back("taskbar-hide");
 g_exitCleanupThread=CreateEventW(nullptr,TRUE,TRUE,nullptr);
 need(g_exitCleanupThread!=nullptr,"Private cleanup event was not created");
 hostLocalOwnerReturned=true;
 SendMessageW(root,WM_APP_EXIT_READY,91,0);requireActive();
 need(!g_exitRequested&&exitStarts==0,"READY invented an exit intent");
 intentStages.push_back("ready-without-intent");
 need(ResetEvent(g_exitCleanupThread)!=FALSE,"Could not reset private cleanup event");
 registerAppExit();
 const auto& exit=nativeHandlers.at("app.exit");
 const DWORD windowThread=GetWindowThreadProcessId(root,nullptr);
 DWORD workerThread{};
 Json reply;
 std::exception_ptr workerError;
 const auto previousLogs=noticeDiagnosticReceipts.size();
 g_hwnd=root;
 try{
  std::thread worker([&]{
   workerThread=GetCurrentThreadId();
   try{reply=exit({{"code",73}});}catch(...){workerError=std::current_exception();}
  });
  worker.join();
 }catch(...){g_hwnd=nullptr;throw;}
 g_hwnd=nullptr;
 if(workerError)std::rethrow_exception(workerError);
 requireHide(2);
 need(windowThread==GetCurrentThreadId()&&workerThread!=0&&workerThread!=windowThread&&reply==true,
  "app.exit did not acknowledge from a different thread before native dispatch");
 need(noticeDiagnosticReceipts.size()==previousLogs+1,"Expected exactly one production app.exit diagnostic");
 const auto diagnostic=noticeDiagnosticReceipts.back();
 need(diagnostic==Json{{"event","app-exit-ipc"},{"code",73},{"threadId",workerThread},{"windowAlive",true}},
  "Production app.exit diagnostic mismatched the caller, code, or owned root status");
 MSG message{};
 need(PeekMessageW(&message,root,WM_APP_EXIT,WM_APP_EXIT,PM_REMOVE)!=FALSE&&message.hwnd==root&&message.message==WM_APP_EXIT&&message.wParam==73&&message.lParam==0,
  "Production app.exit did not queue its exit code on the owned root");
 MSG duplicate{};
 need(!PeekMessageW(&duplicate,root,WM_APP_EXIT,WM_APP_EXIT,PM_NOREMOVE),"app.exit queued duplicate shutdown messages");
 appExitIpc={{"handler","app.exit"},{"reply",reply},{"queuedMessages",1},{"code",message.wParam},
  {"workerThreadId",workerThread},{"windowThreadId",windowThread},{"diagnostic",diagnostic}};
 // The worker control observes a separate queue delivery without committing exit.
 // Only the request delivered through the main WebView callback drives teardown.
 const auto requireNoNewExit=[&]{
  MSG pending{};
  requireHide(2);
  need(!unexpectedAsyncDispatch&&noticeDiagnosticReceipts.size()==previousLogs+1&&
   !PeekMessageW(&pending,root,WM_APP_EXIT,WM_APP_EXIT,PM_NOREMOVE),"Rejected IPC reached the exit handler or enqueued work");
 };
 g_hwnd=root;
 for(int mode:{0,1,2}){
  g_asyncMode=mode;
  need(!ipc_cmd_async("app.exit"),"Production selector sent app.exit off the HWND thread");
 }
 unsigned messagesRead=0;
 const auto deliver=[&](std::wstring_view payload){
  ICoreWebView2WebMessageReceivedEventArgs event{payload};
  need(mainWebMessageReceived(g_view.Get(),&event)==S_OK&&event.reads==1,"Main WebView callback did not read one JSON message");
  messagesRead+=event.reads;
 };
 g_permissions.emplace("app.*",false);
 deliver(LR"({"id":801,"cmd":"app.exit","args":{"code":73}})");
 requireNoNewExit();
 const Json denied={{"id",801},{"error","permission denied: app.exit"}};
 need(ipcReplies.size()==1&&ipcReplies.front().body==denied&&ipcReplies.front().childId==0&&
  ipcReplies.front().threadId==windowThread&&ipcReplies.front().rootAlive&&!ipcReplies.front().exitRequested,
  "Permission rejection did not preserve the request ID and ownership");
 deliver(LR"({"id":802,"cmd":"app.exit","args":)");
 requireNoNewExit();
 need(ipcReplies.size()==1,"Malformed request generated a reply");
 g_permissions.clear();
 const auto witness=std::make_shared<WallpaperWitness>();
 g_mainWallpaperWitness=witness;
 deliver(LR"({"id":804,"cmd":"app.exit","args":{"code":73}})");
 requireNoNewExit();
 need(ipcReplies.size()==1&&witness->consumed==1&&witness->matched,"Consumed WebView message reached normal IPC or used the wrong event");
 witness->consume=false;
 witness->matched=false;
 deliver(LR"({"id":803,"cmd":"app.exit","args":{"code":73}})");
 need(g_mainWallpaperWitness==witness&&witness->passed==1&&witness->matched,"Unconsumed WebView message did not pass the witness");
 g_mainWallpaperWitness.reset();
 g_hwnd=nullptr;
 requireHide(2);
 need(!unexpectedAsyncDispatch&&ipcReplies.size()==2&&noticeDiagnosticReceipts.size()==previousLogs+2,
  "Expected one synchronous app.exit reply and diagnostic");
 const auto& sync=ipcReplies.back();
 const auto& uiDiagnostic=noticeDiagnosticReceipts.back();
 need(sync.body==Json{{"id",803},{"result",true}}&&sync.childId==0&&sync.threadId==windowThread&&sync.rootAlive&&!sync.exitRequested&&
  uiDiagnostic==Json{{"event","app-exit-ipc"},{"code",73},{"threadId",windowThread},{"windowAlive",true}},
  "Synchronous app.exit did not reply on the window thread before teardown");
 need(PeekMessageW(&message,root,WM_APP_EXIT,WM_APP_EXIT,PM_REMOVE)!=FALSE&&message.hwnd==root&&message.message==WM_APP_EXIT&&message.wParam==73&&message.lParam==0&&
  !PeekMessageW(&duplicate,root,WM_APP_EXIT,WM_APP_EXIT,PM_NOREMOVE),"Receiver must queue exactly one exit with the supplied code");
 appExitIpc["syncReceiver"]={{"reply",sync.body},{"permissionReply",denied},{"malformedReplyCount",0},
  {"childId",sync.childId},{"threadId",sync.threadId},{"rootAliveAtReply",sync.rootAlive},
  {"exitRequestedAtReply",sync.exitRequested},{"diagnostic",uiDiagnostic}};
 appExitIpc["sourceCallback"]={{"messagesRead",messagesRead},{"witnessConsumed",witness->consumed},{"witnessPassThrough",witness->passed}};
 DispatchMessageW(&message);requireActive();
 need(g_exitRequested&&exitStarts==1&&exitTrigger=="wm-app-exit"&&hides==2,"Explicit exit routed as hide or failed to start");
 intentStages.push_back("explicit-exit");
 const auto event=g_exitCleanupThread;
 const auto waitWithoutTeardown=[&]{
  g_exitReadyPosted=true;
  SendMessageW(root,WM_APP_EXIT_READY,73,0);requireActive();
  need(!g_exitReadyPosted&&g_exitCleanupThread==event,"Waiting branch failed to rearm or keep its handle");
 };
 waitWithoutTeardown();intentStages.push_back("unsignaled-cleanup");
 hostLocalOwnerReturned=false;
 need(SetEvent(event)!=FALSE,"Could not signal private cleanup event");
 waitWithoutTeardown();intentStages.push_back("local-owner-pending");
 hostLocalOwnerReturned=true;
 SendMessageW(root,WM_APP_EXIT_READY,73,0);
 need(!g_exitCleanupThread&&g_exitCleanupCompleted&&route.readyTeardownObserved&&exitStarts==1&&hides==2,
  "Final READY did not complete its teardown before WM_DESTROY");
 intentStages.push_back("ready-destroys");
}
#endif
#ifndef YMCC_TEST_REAL_ROOT_EXIT
static void run(bool normal,bool omit,const std::string& nonce){
 Carrier carrier;Config config;config.noticeOnly=true;carrier.initialize(nullptr,std::move(config));
 auto lease=carrier.reserveNotice(nonce)["leaseId"].get<unsigned long long>();
 const auto windows=CarrierTestAccess::attach(carrier,lease,nonce);
 const auto closes=controllerClosed,destroyed=probe::destroyed,timerBefore=CarrierTestAccess::deliveredTimers(windows);
 pumpUntilOwnTimer(windows,500);
 need(CarrierTestAccess::deliveredTimers(windows)>timerBefore&&controllerLive==1&&IsWindow(windows.first)&&IsWindow(windows.second),
  "Active standalone notice required before WM_DESTROY");
 const auto oldFences=inputFences,oldCcd=ccdStops,oldPool=poolStops;
#ifdef YMCC_TEST_ROOT_EXIT_INTENT
 g_exitRequested=false;
#else
 g_exitRequested=normal;
#endif
 g_ccdStop=false;
 Route route{carrier,omit};
 HWND root=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,L"YMCC_Notice_Root_Destroy_Test",L"Private root",WS_POPUP|WS_DISABLED,
  0,0,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),&route);
 need(root!=nullptr,"Private root HWND was not created");++ownedRoots;
#ifdef YMCC_TEST_ROOT_EXIT_INTENT
 try{driveIntent(root,route,windows,carrier);}catch(...){
  g_hwnd=nullptr;
  if(g_exitCleanupThread){CloseHandle(g_exitCleanupThread);g_exitCleanupThread=nullptr;}
  if(IsWindow(root)){SetWindowLongPtrW(root,GWLP_USERDATA,0);::DestroyWindow(root);}
  if(!IsWindow(root))--ownedRoots;
  throw;
 }
 const BOOL rootDestroyed=!IsWindow(root);
#else
 const BOOL rootDestroyed=::DestroyWindow(root);
#endif
 if(IsWindow(root)){
  SetWindowLongPtrW(root,GWLP_USERDATA,0);
  ::DestroyWindow(root);
 }
 if(!IsWindow(root))--ownedRoots;
 need(rootDestroyed&&!IsWindow(root)&&route.delivered==1,"OS did not deliver WM_DESTROY exactly once to the owned root");
 MSG message{};
 need(PeekMessageW(&message,nullptr,WM_QUIT,WM_QUIT,PM_REMOVE)&&message.message==WM_QUIT&&message.wParam==(normal?73U:0U),
  "Projected branch did not preserve its exit message");
 need(inputFences==oldFences+(normal?0:1)&&ccdStops==oldCcd+(normal?1:0)&&poolStops==oldPool+(normal?0:1)&&g_ccdStop==!normal,
  "Projected branch entered the wrong normal/unexpected terminal");
 need(unexpectedTrayCalls==0,"Root fixture unexpectedly tried to access the tray");
 if(omit){
  need(IsWindow(windows.first)&&IsWindow(windows.second)&&controllerLive==1&&controllerClosed==closes&&probe::destroyed==destroyed&&
   CarrierTestAccess::retained(carrier)&&!CarrierTestAccess::retired(carrier),"Single-call omission failed to leave the resources observable");
  carrier.shutdown(); // Only the negative needs explicit fixture rescue.
 }
 need(!IsWindow(windows.first)&&!IsWindow(windows.second)&&controllerLive==0&&controllerClosed==closes+1&&probe::destroyed==destroyed+2&&
  !CarrierTestAccess::retained(carrier)&&CarrierTestAccess::retired(carrier),"Owned notice resources were not retired before the Carrier destructor");
 const auto timerAfter=CarrierTestAccess::deliveredTimers(windows);
 bool refused=false;
 try{carrier.create(0,0,nonce,true,lease);}catch(const std::exception& e){refused=std::string_view(e.what()).find("lease retired or replaced")!=std::string_view::npos;}
 pump(30);
 need(refused&&!CarrierTestAccess::retained(carrier)&&CarrierTestAccess::deliveredTimers(windows)==timerAfter&&controllerClosed==closes+1,
  "Old lease or timer revived the notice after root destruction");
}
#endif
}
