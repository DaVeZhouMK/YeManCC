// Actual WinRT dispatcher interruptions and production button recovery on a
// private desktop. No SendInput, physical devices, installed app or Steam.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpads.h"
#include <iostream>
#include <thread>
#include <functional>
using namespace ymcc::screenpads;
static Json cases=Json::array(),details=Json::array();static bool passed=true;
static bool runtimeClosed=false,queueClosed=false,workerEnded=false;static DWORD workerError=0;
static HANDLE doneEvent=nullptr;
static void check(const std::string& name,bool ok){cases.push_back({{"name",name},{"ok",ok}});passed=passed&&ok;}
static std::wstring desktopName(HDESK h){wchar_t name[256]{};DWORD bytes=0;return h&&GetUserObjectInformationW(h,UOI_NAME,name,sizeof(name),&bytes)?name:L"";}
static std::wstring inputName(){HDESK h=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);auto name=desktopName(h);if(h)CloseDesktop(h);return name;}
static void pump(){MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}}
static HWND createOwner(){WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC_ButtonAbortFixtureOwner";RegisterClassW(&wc);return CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);}
static void worker(HDESK desktop){
 HWND host=nullptr;bool apartment=false;std::vector<HWND> originals;
 try{
  if(!SetThreadDesktop(desktop))throw std::runtime_error("private desktop binding failed");
  winrt::init_apartment(winrt::apartment_type::single_threaded);apartment=true;
  host=createOwner();if(!host)throw std::runtime_error("private owner unavailable");
  dryRun=true;setNativeInputCandidate(true);setContext("steamdeck",true);
  Config cfg;cfg.enabled=false;cfg.specialMask=3;cfg.specialEnabled=true;cfg.touchFeedback=TouchFeedbackMode::Off;cfg.autoHideSeconds=0;
  initialize(GetModuleHandleW(nullptr),host,cfg,1);pump();
  check("initial-two-native-special-keys-visible",buttonVisibilityState(cfg)["artworkVisibleMask"]==6&&nativeButtonSurfaces[1].active()&&nativeButtonSurfaces[2].active()&&!windowError);
  for(int round=0;round<3;++round){
   int index=round%2+1,other=index==1?2:1;bool called=false,inSync=false;bool callingSync=false;
   const auto oldRetries=buttonInputRecovery.retries,oldRecovered=buttonInputRecovery.recovered;
   auto dispatcher=nativeButtonSurfaces[index].source().Dispatcher();
   auto action=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{called=true;inSync=callingSync;nativeButtonSurfaces[index].close();});
   const bool queued=!called;callingSync=true;const bool restored=buttonNativeInputSync(index,true);callingSync=false;
   check("round-"+std::to_string(round)+"-callback-ran-inside-real-source-sync",queued&&called&&inSync);
   check("round-"+std::to_string(round)+"-aborted-sync-rebound-once",restored&&nativeButtonSurfaces[index].active()&&buttonInputRecovery.retries==oldRetries+1&&buttonInputRecovery.recovered==oldRecovered+1);
   check("round-"+std::to_string(round)+"-both-input-and-artwork-still-visible",buttonVisibilityState(cfg)["inputVisibleMask"]==6&&buttonVisibilityState(cfg)["artworkVisibleMask"]==6&&nativeButtonSurfaces[other].active());
   check("round-"+std::to_string(round)+"-mouse-pass-policy-still-active",nativeButtonSurfaces[index].ensureMousePass()&&nativeButtonSurfaces[other].ensureMousePass());
   details.push_back(buttonVisibilityState(cfg));
  }
  ShowWindow(buttonWindows[1],SW_HIDE);ShowWindow(buttonOutlines[1],SW_HIDE);nativeButtonSurfaces[1].close();
  check("missing-input-detected-with-saved-selection-intact",buttonsNeedRefresh(cfg)&&effectiveSpecialMask(getConfig())==3);
  const bool refreshed=configure(cfg,1,true);
  check("unchanged-settings-repair-native-window-and-artwork",refreshed&&!windowError&&nativeButtonSurfaces[1].active()&&buttonVisibilityState(cfg)["artworkVisibleMask"]==6);
  const auto uploads=nativeButtonSurfaces[1].uploads,retries=buttonInputRecovery.retries;
  check("healthy-settings-avoid-extra-source-refresh",!configure(cfg,1,true)&&nativeButtonSurfaces[1].uploads==uploads&&buttonInputRecovery.retries==retries);
  for(HWND h:buttonWindows)if(h)originals.push_back(h);for(HWND h:buttonOutlines)if(h)originals.push_back(h);
  for(auto& surface:nativeButtonSurfaces)if(surface.child())originals.push_back(surface.child());
  bool terminalCalled=false;auto dispatcher=nativeButtonSurfaces[1].source().Dispatcher();
  auto terminal=dispatcher.RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{terminalCalled=true;quiesce();});
  refresh();
  check("exit-during-dispatch-is-not-retried-or-reshown",terminalCalled&&inputTeardown&&!initialized&&buttonInputRecovery.retries==retries&&!buttonsVisible());
  check("late-settings-cannot-revive-terminal-owner",!configure(cfg,1,true)&&!buttonsVisible());
  shutdown();
  bool gone=true;for(HWND h:originals)gone=gone&&!IsWindow(h);
  check("all-original-owned-windows-destroyed",gone);
  check("terminal-release-confirmed-three-times",exitInputReleaseState().value("verified",false));
 }catch(const winrt::hresult_error& e){passed=false;workerError=(DWORD)e.code();details.push_back({{"winrtError",(long)e.code()}});shutdown();}
 catch(const std::exception& e){passed=false;details.push_back({{"exception",e.what()}});shutdown();}
 try{
  nativeInputRuntime.compositor=nullptr;
  if(nativeInputRuntime.queue){auto action=nativeInputRuntime.queue.ShutdownQueueAsync();const auto deadline=GetTickCount64()+4000;
   while(action.Status()==winrt::Windows::Foundation::AsyncStatus::Started&&GetTickCount64()<deadline){pump();MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
   queueClosed=action.Status()==winrt::Windows::Foundation::AsyncStatus::Completed;if(queueClosed)action.GetResults();nativeInputRuntime.queue=nullptr;
  }else queueClosed=true;
  runtimeClosed=!nativeInputRuntime.queue&&!nativeInputRuntime.compositor;
 }catch(...){passed=false;}
 if(host)DestroyWindow(host);if(apartment)winrt::uninit_apartment();workerEnded=true;SetEvent(doneEvent);
}
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;const auto inputBefore=inputName();const auto name=L"YMCC_ButtonAbort_"+std::to_wstring(GetCurrentProcessId());
 HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop)return 3;
 doneEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);std::thread task(worker,desktop);
 if(WaitForSingleObject(doneEvent,20000)!=WAIT_OBJECT_0){Json fail={{"ok",false},{"error","worker-timeout"},{"cleanupProven",false}};std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<fail.dump(2);file.close();ExitProcess(4);}
 task.join();CloseHandle(doneEvent);const bool closed=CloseDesktop(desktop)!=FALSE;const bool inputUnchanged=inputBefore==inputName();
 check("private-desktop-closed",closed);check("input-desktop-unchanged",inputUnchanged);check("runtime-and-dispatcher-queue-closed",runtimeClosed&&queueClosed&&workerEnded);
 Json result={{"ok",passed},{"cases",cases},{"details",details},{"checks",cases.size()},{"workerError",workerError},{"realWinRT",true},{"hiddenDesktop",true},{"inputDesktopUnchanged",inputUnchanged},{"runtimeClosed",runtimeClosed},{"queueClosed",queueClosed},{"workerEnded",workerEnded},{"privateDesktopClosed",closed},{"systemInputInjected",false},{"productStarted",false},{"deviceWrites",0}};
 std::ofstream file(std::filesystem::path(argv[1]),std::ios::binary);file<<result.dump(2);std::cout<<"checks="<<cases.size()<<" ok="<<passed<<"\n";return passed?0:1;
}
