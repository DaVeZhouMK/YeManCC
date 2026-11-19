// Real WinRT source recovery on an owned private desktop. No pointer/key
// injection, physical device output, application startup or input-desktop switch.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define YMCC_SCREEN_NATIVE_INPUT_CANDIDATE 1
#include "screen_touchpad_owner.h"
#include <iostream>
#include <string>
using namespace ymcc::screenpads;
static Json cases=Json::array(),details=Json::array();
static bool passed=true;
static void check(const std::string& name,bool ok){cases.push_back({{"name",name},{"ok",ok}});passed=passed&&ok;}
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static std::wstring nameOf(HDESK desktop){wchar_t name[256]{};DWORD bytes=0;return desktop&&GetUserObjectInformationW(desktop,UOI_NAME,name,sizeof(name),&bytes)?name:L"";}
static std::wstring inputDesktop(){const auto desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);const auto name=nameOf(desktop);if(desktop)CloseDesktop(desktop);return name;}
static LRESULT CALLBACK ownerProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==kOwnerRebindMessage){consumeOwnerRebind((UINT_PTR)w);return 0;}
    if(m==kOwnerVisibilityMessage){consumeOwnerVisibility((UINT_PTR)w);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static void recoveryCase(const char* prefix,const Config& cfg){
    const std::string label(prefix);const HWND root=windows[0],child=nativeInputSurfaces[0].child();
    const bool ready=root&&child&&nativeInputSurfaces[0].active()&&nativeInputSurfaces[0].ensureMousePass()&&ownerResumeControlsHealthy();
    check(label+"-real-native-source-and-mouse-pass-ready",ready);
    if(!ready)return;
    RECT rect{};if(!GetWindowRect(root,&rect))throw std::runtime_error("source rectangle unavailable");
    const bool shrunk=SetWindowPos(root,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
    const bool failed=!nativeInputSurfaces[0].sync();const DWORD actualError=(DWORD)nativeInputSurfaces[0].error;
    if(failed){windowError=actualError;recordNativeInputFailure(false,0,nativeInputSurfaces[0]);}
    const bool moved=SetWindowPos(root,nullptr,rect.left,rect.top,rect.right-rect.left,rect.bottom-rect.top,SWP_NOZORDER|SWP_NOACTIVATE)!=FALSE;
    const bool rebound=nativeInputSurfaces[0].sync();const bool frame=renderOutline(0,cfg.transparency,true);
    ShowWindow(root,SW_SHOWNOACTIVATE);ShowWindow(outlines[0],SW_SHOWNOACTIVATE);
    const bool onlyError=actualError&&windowError==actualError&&!padsNeedRefresh(cfg)&&!buttonsNeedRefresh(cfg)&&!ymcc::customtouch::needsRefresh();
    check(label+"-real-failure-recovers-source-before-reselection",shrunk&&failed&&moved&&rebound&&frame&&onlyError);
    const bool passive=configure(cfg,0,false);
    check(label+"-passive-config-does-not-spend-error-recovery",!passive&&windowError==actualError&&nativeInputSurfaces[0].child()==child);
    const bool selected=configure(cfg,0,true);
    check(label+"-explicit-reselection-clears-error",selected&&!windowError&&ownerResumeControlsHealthy());
    check(label+"-healthy-source-identity-is-preserved",nativeInputSurfaces[0].child()==child&&nativeInputSurfaces[0].active());
    details.push_back({{"mode",prefix},{"actualSourceFailure",actualError},{"onlyRetainedError",onlyError},{"passiveRefreshed",passive},{"explicitRefreshed",selected},{"errorAfter",windowError}});
}
static bool closeQueue(ymcc::screeninput::Runtime& runtime){
    runtime.compositor=nullptr;if(!runtime.queue)return true;
    auto action=runtime.queue.ShutdownQueueAsync();const auto deadline=GetTickCount64()+4000;
    while(action.Status()==winrt::Windows::Foundation::AsyncStatus::Started&&GetTickCount64()<deadline){pump();MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
    if(action.Status()!=winrt::Windows::Foundation::AsyncStatus::Completed)return false;
    action.GetResults();runtime.queue=nullptr;return true;
}
static int worker(const wchar_t* output,const wchar_t* expectedDesktop){
    const auto before=inputDesktop();HWND ownerWindow=nullptr;bool apartment=false;
    try{
        check("worker-started-on-parent-owned-private-desktop",nameOf(GetThreadDesktop(GetCurrentThreadId()))==expectedDesktop&&before!=expectedDesktop);
        if(!passed)throw std::runtime_error("private desktop admission failed");
        winrt::init_apartment(winrt::apartment_type::single_threaded);apartment=true;
        WNDCLASSW wc{};wc.lpfnWndProc=ownerProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"YMCC_ManualErrorReselectOwner";
        if(!RegisterClassW(&wc)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)throw std::runtime_error("owner class failed");
        ownerWindow=CreateWindowExW(0,wc.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,wc.hInstance,nullptr);if(!ownerWindow)throw std::runtime_error("owner window failed");
        dryRun=true;Config cfg;cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;cfg.touchFeedback=TouchFeedbackMode::Off;cfg.autoHideSeconds=0;
        cfg.standaloneSpecialMode=StandaloneSpecialMode::SteamDeck;cfg.specialMask=3;
        setContext("disabled",false);check("production-owner-initializes",initializeForOwner(GetModuleHandleW(nullptr),ownerWindow,cfg,0,L"owned.exe"));
        if(!passed)throw std::runtime_error("owner initialization failed");
        recoveryCase("normal",cfg);
        // Retire all current bindings before changing the input apartment.
        // This test never bypasses terminal source confirmation.
        release();for(int i=0;i<2;++i)nativeInputClose(i);buttonsNativeInputDisable();
        bool retired=buttonsOpenInputSources()==0;for(const auto& source:nativeInputSurfaces)retired=retired&&source.terminalReleased();
        check("normal-bindings-retired-before-isolated-channel",retired);
        if(!retired||!nativePadInputRuntime.useFreshInputChannel())throw std::runtime_error("isolated channel admission failed");
        refresh(true);recoveryCase("isolated",cfg);
    }catch(const winrt::hresult_error& error){check("worker-winrt-exception",false);details.push_back({{"hresult",(DWORD)error.code()}});}
    catch(const std::exception& error){check("worker-exception",false);details.push_back({{"error",error.what()}});}
    shutdownForOwner();check("exit-three-clean-input-confirmations",exitInputReleaseState().value("verified",false));
    if(ownerWindow)DestroyWindow(ownerWindow);
    bool padQueueClosed=false,normalQueueClosed=false;
    try{padQueueClosed=closeQueue(nativePadInputRuntime);}catch(...){}
    try{normalQueueClosed=closeQueue(nativeInputRuntime);}catch(...){}
    const bool queuesClosed=padQueueClosed&&normalQueueClosed;
    check("owned-composition-and-dispatcher-queues-close",queuesClosed);
    check("worker-input-desktop-unchanged",!before.empty()&&before==inputDesktop());
    check("no-sender-input-or-held-signal-produced",injectedCount==0&&sentKeys==0&&!sentClick&&screenButtonMask.load()==0);
    if(apartment&&queuesClosed)winrt::uninit_apartment();
    const Json result={{"ok",passed},{"cases",cases},{"details",details},{"checks",cases.size()},{"queuesClosed",queuesClosed},
        {"systemInputInjected",false},{"deviceWrites",0},{"productStarted",false},{"processCpuMeasured",false},{"visibleDesktopSwitched",false}};
    std::ofstream file(std::filesystem::path(output),std::ios::binary);file<<result.dump(2)<<'\n';file.close();
    if(!queuesClosed)std::_Exit(3);return passed&&file.good()?0:1;
}
static std::wstring quote(const std::wstring& value){if(value.find(L'"')!=std::wstring::npos)throw std::runtime_error("unexpected quote in owned path");return L"\""+value+L"\"";}
int wmain(int argc,wchar_t** argv){
    if(argc==4&&std::wstring(argv[1])==L"--owned-worker")return worker(argv[2],argv[3]);
    if(argc!=2)return 2;
    const auto before=inputDesktop();const auto desktopName=L"YMCC_ManualError_"+std::to_wstring(GetCurrentProcessId())+L"_"+std::to_wstring(GetTickCount64());
    HDESK desktop=nullptr;HANDLE job=nullptr;PROCESS_INFORMATION child{};bool exited=false,zero=false,forced=false;DWORD childCode=2;
    Json report={{"ok",false},{"cases",Json::array()},{"forcedTermination",false}};
    try{
        desktop=CreateDesktopW(desktopName.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);if(!desktop)throw std::runtime_error("private desktop creation failed");
        job=CreateJobObjectW(nullptr,nullptr);JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};limits.BasicLimitInformation.LimitFlags=JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if(!job||!SetInformationJobObject(job,JobObjectExtendedLimitInformation,&limits,sizeof(limits)))throw std::runtime_error("owned job setup failed");
        wchar_t self[32768]{};if(!GetModuleFileNameW(nullptr,self,(DWORD)std::size(self)))throw std::runtime_error("self path unavailable");
        const auto childFile=std::filesystem::path(argv[1]).wstring()+L".child.json";
        auto command=quote(self)+L" --owned-worker "+quote(childFile)+L" "+quote(desktopName);auto privateName=desktopName;STARTUPINFOW startup{sizeof(startup)};startup.lpDesktop=privateName.data();
        if(!CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_SUSPENDED|CREATE_NO_WINDOW,nullptr,nullptr,&startup,&child))throw std::runtime_error("private worker creation failed");
        if(!AssignProcessToJobObject(job,child.hProcess)){forced=true;TerminateProcess(child.hProcess,3);WaitForSingleObject(child.hProcess,5000);throw std::runtime_error("worker job admission failed");}
        if(ResumeThread(child.hThread)==(DWORD)-1)throw std::runtime_error("worker resume failed");
        exited=WaitForSingleObject(child.hProcess,30000)==WAIT_OBJECT_0;
        if(!exited){forced=true;TerminateJobObject(job,3);WaitForSingleObject(child.hProcess,5000);throw std::runtime_error("worker deadline exceeded");}
        if(!GetExitCodeProcess(child.hProcess,&childCode))throw std::runtime_error("worker exit unavailable");
        std::ifstream input(std::filesystem::path(childFile),std::ios::binary);if(!(input>>report))throw std::runtime_error("worker report missing");
        const auto deadline=GetTickCount64()+5000;JOBOBJECT_BASIC_ACCOUNTING_INFORMATION accounting{};
        do{if(!QueryInformationJobObject(job,JobObjectBasicAccountingInformation,&accounting,sizeof(accounting),nullptr))throw std::runtime_error("job observation failed");zero=accounting.ActiveProcesses==0;if(zero)break;Sleep(10);}while(GetTickCount64()<deadline);
        report["jobActiveBeforeClose"]=accounting.ActiveProcesses;report["childExitCode"]=childCode;
        if(!zero){forced=true;throw std::runtime_error("owned job has remaining processes");}
    }catch(const std::exception& error){report["ok"]=false;report["launcherError"]=error.what();report["launcherWin32Error"]=GetLastError();}
    if(child.hProcess&&WaitForSingleObject(child.hProcess,0)!=WAIT_OBJECT_0){forced=true;if(job)TerminateJobObject(job,3);else TerminateProcess(child.hProcess,3);WaitForSingleObject(child.hProcess,5000);}
    if(job)CloseHandle(job);if(child.hThread)CloseHandle(child.hThread);if(child.hProcess)CloseHandle(child.hProcess);
    const bool closed=desktop&&CloseDesktop(desktop)!=FALSE,unchanged=!before.empty()&&before==inputDesktop();
    report["cases"].push_back({{"name","parent-closes-private-desktop-after-natural-worker-exit"},{"ok",closed&&exited&&zero}});
    report["cases"].push_back({{"name","parent-input-desktop-unchanged"},{"ok",unchanged}});
    report["privateDesktopClosed"]=closed;report["parentInputDesktopUnchanged"]=unchanged;report["checks"]=report["cases"].size();report["forcedTermination"]=forced;
    report["ok"]=report.value("ok",false)&&childCode==0&&closed&&zero&&unchanged&&!forced;
    std::ofstream output(std::filesystem::path(argv[1]),std::ios::binary);output<<report.dump(2)<<'\n';
    std::cout<<"MANUAL_ERROR_RESELECT checks="<<report["checks"]<<" ok="<<report["ok"]<<'\n';return report.value("ok",false)&&output.good()?0:1;
}
