// Production touchpad/visibility code on a never-displayed desktop. Two owned
// lower processes emulate game switches. Native hit testing, not click replay.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static HWND revealFaultWindow=nullptr;
static HWND secondRevealFaultWindow=nullptr;
static unsigned revealFaultsRemaining=0,revealSubmissions=0,revealSubmissionsAtAction=0,revealSubmissionsAfterAction=0;
static unsigned secondRevealFaultsRemaining=0,secondRevealSubmissions=0;
static void (*revealFaultAction)()=nullptr;
static void (*revealSuccessAction)()=nullptr;
static BOOL WINAPI fixtureRevealLayeredWindow(HWND h,HDC dst,POINT* pos,SIZE* size,HDC src,POINT* origin,COLORREF key,BLENDFUNCTION* blend,DWORD flags){
    if(h==secondRevealFaultWindow){
        ++secondRevealSubmissions;
        if(secondRevealFaultsRemaining){--secondRevealFaultsRemaining;SetLastError(ERROR_BUSY);return FALSE;}
    }
    if(h==revealFaultWindow){
        ++revealSubmissions;
        if(revealFaultsRemaining){
            --revealFaultsRemaining;
            if(const auto action=revealFaultAction){revealFaultAction=nullptr;revealSubmissionsAtAction=revealSubmissions;action();revealSubmissionsAfterAction=revealSubmissions;}
            SetLastError(ERROR_RETRY);return FALSE;
        }
    }
    const BOOL ok=UpdateLayeredWindow(h,dst,pos,size,src,origin,key,blend,flags);
    if(ok&&h==revealFaultWindow)if(const auto action=revealSuccessAction){revealSuccessAction=nullptr;action();}
    return ok;
}
static bool rejectNextReflowTimer=false;
static unsigned reflowTimerRejections=0;
static UINT_PTR WINAPI fixtureReflowSetTimer(HWND h,UINT_PTR id,UINT delay,TIMERPROC proc){
    // Only the selected one-shot for display reflow; keep all other live Win32 timers.
    if(rejectNextReflowTimer&&delay==100&&(id&0xffff0000u)==0x59560000u){
        rejectNextReflowTimer=false;++reflowTimerRejections;SetLastError(ERROR_NOT_ENOUGH_MEMORY);return 0;
    }
    return SetTimer(h,id,delay,proc);
}
#define SetTimer fixtureReflowSetTimer
#define UpdateLayeredWindow fixtureRevealLayeredWindow
static HWND editorDestroyFaultWindow=nullptr;
static int editorDestroyFailuresRemaining=0;
static unsigned editorDestroyAttempts=0;
static BOOL WINAPI fixtureEditorDestroyWindow(HWND window){
    if(window==editorDestroyFaultWindow){
        ++editorDestroyAttempts;
        if(editorDestroyFailuresRemaining){
            if(editorDestroyFailuresRemaining>0)--editorDestroyFailuresRemaining;
            SetLastError(ERROR_BUSY);return FALSE;
        }
    }
    return DestroyWindow(window);
}
#define DestroyWindow fixtureEditorDestroyWindow
#include <screen_touchpad_owner.h>
#undef DestroyWindow
#undef UpdateLayeredWindow
#undef SetTimer
#include <future>
#include <iostream>
using namespace ymcc::screenpads;
static constexpr UINT promoteMessage=WM_APP+701,shapeMessage=WM_APP+702;
static Json cases=Json::array();static bool passed=true;
static void check(const std::string& name,bool ok){passed=passed&&ok;cases.push_back({{"name",name},{"ok",ok}});}
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static unsigned ownerSnapshotCaptures=0;
static bool exitReleaseObserved=false,exitReleaseWithdrawn=false;
static void observeCustomExitRelease(){
    using namespace ymcc::customtouch;
    const auto inert=[](HWND h){return h&&IsWindow(h)&&!IsWindowVisible(h)&&!IsWindowEnabled(h);};
    exitReleaseObserved=true;
    exitReleaseWithdrawn=inputTeardown&&inert(windows[0])&&inert(windows[1])&&inert(nativeInputSurfaces[0].child())&&
        inert(buttonWindows[1])&&inert(buttonWindows[2])&&inert(nativeButtonSurfaces[1].child())&&
        items.size()==1&&inert(items[0]->window)&&inert(items[0]->surface.child())&&!IsWindowVisible(items[0]->outline)&&
        (!toolbar||inert(toolbar));
}
static LRESULT CALLBACK targetProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==promoteMessage){SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(h);return 1;}
    if(m==shapeMessage){
        const bool borderless=(w%2)==0;SetWindowLongPtrW(h,GWL_STYLE,borderless?WS_POPUP:WS_OVERLAPPEDWINDOW);
        if(w%4==2)ShowWindow(h,SW_MINIMIZE);
        ShowWindow(h,SW_SHOWNOACTIVATE);
        // Overlapped windows have maximum tracking sizes; a huge negative
        // desktop rectangle can be clamped entirely off-screen. Fit a real
        // monitor client rectangle instead of assuming popup size semantics.
        MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTOPRIMARY),&mi);RECT r=mi.rcMonitor;
        if(!borderless)AdjustWindowRectExForDpi(&r,WS_OVERLAPPEDWINDOW,FALSE,GetWindowLongW(h,GWL_EXSTYLE),GetDpiForWindow(h));
        SetWindowPos(h,HWND_TOPMOST,r.left,r.top,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_FRAMECHANGED|SWP_SHOWWINDOW);
        return 1;
    }
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK ownerProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==kOwnerVisibilityMessage){consumeOwnerVisibility((UINT_PTR)w);return 0;}
    if(m==kOwnerExportMessage){consumeOwnerExport(h,(UINT_PTR)w,[]{
        ++ownerSnapshotCaptures;
        return Json{{"ownerThread",GetCurrentThreadId()},{"onOwner",onOwnerThread(owner.load())},
            {"state",state(true)},{"displayStack",ownerVisibilityState()}};
    });return 0;}
    if(m==kOwnerRebindMessage){consumeOwnerRebind((UINT_PTR)w);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static WNDPROC previousRebindProbeProc=nullptr;
static WNDPROC previousRepairCloseProbeProc=nullptr;
static bool repairCloseArmed=false,repairCloseReentry=false;
static LRESULT CALLBACK reflowOnRepairClose(HWND h,UINT m,WPARAM w,LPARAM l){
    if(repairCloseArmed&&m==WM_STYLECHANGED&&w==GWL_EXSTYLE&&l){
        const auto& style=*reinterpret_cast<const STYLESTRUCT*>(l);
        if((style.styleOld&WS_EX_LAYERED)&&!(style.styleNew&WS_EX_LAYERED)){
            repairCloseArmed=false;repairCloseReentry=queueOwnerDisplayRefresh(true);
        }
    }
    return CallWindowProcW(previousRepairCloseProbeProc,h,m,w,l);
}
static WNDPROC previousReflowShowProc=nullptr;
static void (*reflowAdmissionProbe)()=nullptr;
static LRESULT CALLBACK observeReflowShow(HWND h,UINT m,WPARAM w,LPARAM l){
    const auto result=CallWindowProcW(previousReflowShowProc,h,m,w,l);
    if(m==WM_WINDOWPOSCHANGED&&l&&reflowAdmissionProbe&&
       (reinterpret_cast<const WINDOWPOS*>(l)->flags&SWP_SHOWWINDOW)&&IsWindowVisible(h)){
        const auto probe=reflowAdmissionProbe;reflowAdmissionProbe=nullptr;probe();
    }
    return result;
}
static bool suspendEnteredDuringRebind=false;
static LRESULT CALLBACK suspendOnOldRebindClose(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==WM_STYLECHANGED&&w==GWL_EXSTYLE&&l&&!suspendEnteredDuringRebind){
        const auto& style=*reinterpret_cast<const STYLESTRUCT*>(l);
        if((style.styleOld&WS_EX_LAYERED)&&!(style.styleNew&WS_EX_LAYERED)){
            suspendEnteredDuringRebind=true;ownerOnSuspend();
        }
    }
    return CallWindowProcW(previousRebindProbeProc,h,m,w,l);
}
static HWND create(const wchar_t* name,WNDPROC proc,DWORD ex,DWORD style,HWND parent=nullptr){
    WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=name;c.lpfnWndProc=proc;
    if(!RegisterClassW(&c)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
    return CreateWindowExW(ex,name,L"Owned hidden desktop fixture",style,-8000,-8000,16000,16000,parent,nullptr,c.hInstance,nullptr);
}
struct Target{PROCESS_INFORMATION pi{};HWND window=nullptr;};
static bool message(HWND window,UINT m,WPARAM w=0){DWORD_PTR r=0;return SendMessageTimeoutW(window,m,w,0,SMTO_BLOCK|SMTO_ABORTIFHUNG,3000,&r)!=0;}
static bool start(Target& t,const std::wstring& directory,const std::wstring& desktop,int index){
    wchar_t self[32768]{};if(!GetModuleFileNameW(nullptr,self,32768))return false;
    const std::wstring ready=directory+L"\\target-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(index)+L".txt";
    std::wstring command=L"\""+std::wstring(self)+L"\" --target \""+ready+L"\"",desk=desktop;
    STARTUPINFOW si{sizeof(si)};si.lpDesktop=desk.data();si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    if(!CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&t.pi))return false;
    CloseHandle(t.pi.hThread);t.pi.hThread=nullptr;
    for(int retry=0;retry<100;retry++){
        std::ifstream f{std::filesystem::path(ready)};unsigned long long hwnd=0;
        if(f>>hwnd){
            const auto candidate=(HWND)(ULONG_PTR)hwnd;DWORD pid=0;
            // A retained ready file can belong to an earlier parent with the same PID.
            if(GetWindowThreadProcessId(candidate,&pid)&&pid==t.pi.dwProcessId){t.window=candidate;return true;}
        }
        if(WaitForSingleObject(t.pi.hProcess,20)!=WAIT_TIMEOUT)break;
    }
    return false;
}
static bool stop(Target& t){
    if(!t.pi.hProcess)return true;DWORD pid=0;if(t.window&&GetWindowThreadProcessId(t.window,&pid)&&pid==t.pi.dwProcessId)PostMessageW(t.window,WM_CLOSE,0,0);
    const bool done=WaitForSingleObject(t.pi.hProcess,5000)==WAIT_OBJECT_0;CloseHandle(t.pi.hProcess);t.pi.hProcess=nullptr;return done;
}
static bool below(HWND target,int round,Json& hitRows){
    bool ok=true;
    for(int side=0;side<2;++side)if(windows[side]&&IsWindowVisible(windows[side])){
        RECT r{};if(!GetWindowRect(windows[side],&r))return false;
        const POINT points[]={{(r.left+r.right)/2,(r.top+r.bottom)/2},{r.left+2,(r.top+r.bottom)/2},
            {r.right-3,(r.top+r.bottom)/2},{(r.left+r.right)/2,r.top+2},{(r.left+r.right)/2,r.bottom-3},
            {r.left+2,r.top+2},{r.right-3,r.bottom-3}};
        for(const auto point:points){const HWND hit=WindowFromPoint(point);const bool pass=hit==target;ok=ok&&pass;
            hitRows.push_back({{"round",round},{"surface",side},{"x",point.x},{"y",point.y},{"passed",pass},{"hit",(ULONG_PTR)hit},{"target",(ULONG_PTR)target}});}
    }
    for(int i=0;i<7;++i)if(buttonWindows[i]&&IsWindowVisible(buttonWindows[i])){
        RECT r{};GetWindowRect(buttonWindows[i],&r);const POINT p{(r.left+r.right)/2,(r.top+r.bottom)/2};
        const HWND hit=WindowFromPoint(p);const bool pass=hit==target;ok=ok&&pass;
        hitRows.push_back({{"round",round},{"button",i},{"passed",pass},{"hit",(ULONG_PTR)hit},{"target",(ULONG_PTR)target}});
    }
    return ok;
}
static void mutate(int kind){
    const HWND h=windows[0];const auto style=GetWindowLongPtrW(h,GWL_EXSTYLE);
    switch(kind){
    case 0:SetWindowLongPtrW(h,GWL_EXSTYLE,style&~WS_EX_LAYERED);break;
    case 1:SetWindowLongPtrW(outlines[0],GWL_EXSTYLE,GetWindowLongPtrW(outlines[0],GWL_EXSTYLE)&~(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE));break;
    case 2:SetWindowLongPtrW(h,GWL_EXSTYLE,style|WS_EX_TRANSPARENT);break;
    case 3:SetWindowLongPtrW(h,GWL_EXSTYLE,style&~WS_EX_NOACTIVATE);break;
    case 4:SetLayeredWindowAttributes(h,0,255,LWA_ALPHA);break;
    case 5:EnableWindow(outlines[0],TRUE);break;
    case 6:{const HWND child=nativeInputSurfaces[0].child();EnableWindow(child,FALSE);SetWindowLongPtrW(child,GWL_EXSTYLE,GetWindowLongPtrW(child,GWL_EXSTYLE)&~WS_EX_NOACTIVATE);break;}
    case 7:EnableWindow(h,FALSE);break;
    }
}
int wmain(int argc,wchar_t** argv){
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    if(argc==3&&std::wstring(argv[1])==L"--target"){
        HWND h=create(L"YMCC_TouchGameSwitchLower",targetProc,WS_EX_TOPMOST,WS_POPUP);if(!h)return 3;
        ShowWindow(h,SW_SHOWNOACTIVATE);SetTimer(h,1,60000,[](HWND h,UINT,UINT_PTR,DWORD){PostMessageW(h,WM_CLOSE,0,0);});
        std::ofstream f{std::filesystem::path(argv[2])};f<<(ULONG_PTR)h;f.close();
        MSG m{};while(GetMessageW(&m,nullptr,0,0)>0){TranslateMessage(&m);DispatchMessageW(&m);}return 0;
    }
    const bool artworkOnly=argc==3&&std::wstring(argv[2])==L"--artwork-restore";
    const bool preservationOnly=argc==3&&std::wstring(argv[2])==L"--artwork-preservation";
    const bool autoHideOnly=argc==3&&std::wstring(argv[2])==L"--switch-storm-autohide";
    const bool verificationOnly=argc==3&&std::wstring(argv[2])==L"--switch-storm-verification";
    const bool transitionsOnly=argc==3&&std::wstring(argv[2])==L"--switch-storm-transitions";
    const bool visibilityFaultOnly=argc==3&&std::wstring(argv[2])==L"--visibility-fault-only";
    const bool stormOnly=autoHideOnly||verificationOnly||transitionsOnly||visibilityFaultOnly||(argc==3&&std::wstring(argv[2])==L"--switch-storm");
    if(argc!=2&&!artworkOnly&&!preservationOnly&&!stormOnly)return 2;
    const auto output=std::filesystem::path(argv[1]),directory=output.parent_path();
    Json report={{"ok",false},{"rounds",(artworkOnly||preservationOnly||visibilityFaultOnly)?0:64},{"hiddenDesktop",true},{"visibleDesktopSwitched",false},{"systemInputInjected",false},
        {"nativeHitTesting",true},{"clickForwarded",false},{"hardwareWrites",0},{"productDeployed",false},
        {"actualGame",false},{"hardwareTouchVerified",false},{"hitRows",Json::array()}};
    const HDESK original=GetThreadDesktop(GetCurrentThreadId());
    const std::wstring name=L"YMCC_TouchGameSwitch_"+std::to_wstring(GetCurrentProcessId());
    HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    if(!desktop||!SetThreadDesktop(desktop)){report["failure"]="hidden desktop unavailable";std::ofstream f(output);f<<report.dump(2);return 4;}
    Target targets[2];HWND ownerWindow=nullptr;bool terminal=true;bool apartment=false;
    try{
        wchar_t station[1024]{};DWORD bytes=0;if(!GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station,sizeof(station),&bytes))throw std::runtime_error("station unavailable");
        const std::wstring desk=std::wstring(station)+L"\\"+name;
        if(!start(targets[0],directory.wstring(),desk,0)||!start(targets[1],directory.wstring(),desk,1))throw std::runtime_error("owned lower-process startup failed");
        report["targetPids"]={targets[0].pi.dwProcessId,targets[1].pi.dwProcessId};
        winrt::init_apartment(winrt::apartment_type::single_threaded);apartment=true;
        ownerWindow=create(L"YMCC_TouchGameSwitchOwner",ownerProc,0,0,HWND_MESSAGE);if(!ownerWindow)throw std::runtime_error("owner creation failed");
        dryRun=true;Config cfg;cfg.enabled=true;cfg.layout=Layout::Single;cfg.single=Mode::Mouse;
        cfg.standaloneSpecialMode=StandaloneSpecialMode::SteamDeck;cfg.specialMask=3;
        setContext("disabled",false);if(!initializeForOwner(GetModuleHandleW(nullptr),ownerWindow,cfg,0,L"owned.exe"))throw std::runtime_error("production owner init failed");
        if(visibilityFaultOnly){
            report["selector"]="--visibility-fault-only";
            cfg.autoHideSeconds=0;configure(cfg,0,true);pump();
            stopOwnerVisibilityRechecks();
            static ymcc::sleeppower::SharedPowerV1 power;
            power={};power.generation=811;power.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
            ownerPowerCapture=[](ymcc::sleeppower::SharedPowerV1& value){value=power;return true;};
            ownerPowerCurrent=[](const ymcc::sleeppower::SharedPowerV1& value){return value.generation==power.generation&&ymcc::sleeppower::powerAdmitted(power);};
            const auto saved=configJson(getConfig());
            const auto runFailure=[&](bool button,int index){
                const auto label=std::string(button?"button":"pad");
                const HWND root=button?buttonWindows[index]:windows[index];
                const HWND art=button?buttonOutlines[index]:outlines[index];
                auto& surface=button?nativeButtonSurfaces[index]:nativeInputSurfaces[index];
                const auto child=surface.child();const auto before=nativeInputFailure.sequence;
                if(!surface.active()||!IsWindowVisible(root)||!DestroyWindow(child))throw std::runtime_error("fixture child retirement failed");
                restoreScreenLayerPair(root,art,targets[0].window,true);
                check(label+"-real-validation-failure-still-hides-both-layers",FAILED(surface.error)&&!IsWindowVisible(root)&&!IsWindowVisible(art));
                const bool routed=nativeInputFailure.sequence==before+1&&nativeInputFailure.error==(DWORD)surface.error&&
                    nativeInputFailure.button==button&&nativeInputFailure.index==index&&ownerResumeRecovery.pending&&ownerResumeRecovery.timer;
                check(label+"-validation-failure-starts-existing-recovery",routed);
                if(!routed)return;
                const auto timer=ownerResumeRecovery.timer,deadline=ownerResumeRecovery.deadline;
                restoreScreenLayerPair(root,art,targets[0].window,true);
                check(label+"-hidden-failure-does-not-repeat-or-rearm",nativeInputFailure.sequence==before+1&&ownerResumeRecovery.timer==timer&&ownerResumeRecovery.deadline==deadline);
                for(int i=0;i<5&&ownerResumeRecovery.pending;++i){const auto r=ownerResumeRecovery;ownerResumeTimerProc(r.window,WM_TIMER,r.timer,0);}
                check(label+"-bounded-recovery-restores-source-and-settings",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&
                    ownerResumeRecovery.clean==2&&ownerResumeRecovery.repairs==1&&IsWindowVisible(root)&&surface.active()&&configJson(getConfig())==saved);
            };
            runFailure(false,0);
            if(passed){++power.generation;runFailure(true,1);}
            ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;
        }else if(autoHideOnly){
            cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=15;cfg.touchFeedback=TouchFeedbackMode::Off;
            setContext("steamdeck",true);configure(cfg,1,true);pump();
            static ymcc::sleeppower::SharedPowerV1 observed;observed={};observed.generation=710;observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
            ownerPowerCapture=[](ymcc::sleeppower::SharedPowerV1& out){out=observed;return true;};
            ownerPowerCurrent=[](const ymcc::sleeppower::SharedPowerV1& p){return p.generation==observed.generation&&p.suspendSerial==observed.suspendSerial&&ymcc::sleeppower::powerAdmitted(observed);};
            autoHideSetHidden(true);ownerOnSuspend();queueOwnerResumeRebind();pump();ownerResumeReady(observed.generation);
            const auto uploads=outlineUploadCount;const auto child=nativeInputSurfaces[0].child();
            for(int i=0;i<2;++i){const auto r=ownerResumeRecovery;ownerResumeTimerProc(r.window,WM_TIMER,r.timer,0);}
            const bool hidden=ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&!IsWindowVisible(outlines[0])&&autoHide.hidden&&outlineUploadCount==uploads;
            check("verify-auto-hidden-source-health-does-not-reshow-artwork",hidden);if(!passed)throw std::runtime_error("autohide-health-failed");
            check("verify-auto-hidden-probe-retains-source-with-zero-repairs",nativeInputSurfaces[0].child()==child&&ownerResumeRecovery.repairs==0&&ownerResumeControlsHealthy());if(!passed)throw std::runtime_error("autohide-source-failed");
            configure(cfg,1,true);const bool queued=ownerVisibilityArtworkRequested;
            check("verify-explicit-reselect-reveals-and-queues-before-game-toggle",queued&&!autoHide.hidden&&IsWindowVisible(outlines[0])&&nativeInputSurfaces[0].child()==child);if(!passed)throw std::runtime_error("explicit-reveal-failed");
            pump();
            const auto rightSource=nativeInputSurfaces[1].child();
            const auto before=eventCount;
            autoHideSetHidden(true);autoHideStopTimer();outlineUploads[1]={};
            revealFaultWindow=outlines[1];revealFaultsRemaining=1;revealSubmissions=0;
            handlePointer(windows[0],0,WM_POINTERDOWN,51,25,25,0);
            const bool accepted=eventCount==before+1;
            const bool firstReveal=!autoHide.hidden&&IsWindowVisible(outlines[0])&&IsWindowVisible(outlines[1]);
            handlePointer(windows[0],0,WM_POINTERUP,51,25,25,0);
            check("verify-failed-reveal-keeps-touch-and-existing-input-sources",accepted&&eventCount==before+2&&
                nativeInputSurfaces[0].child()==child&&nativeInputSurfaces[1].child()==rightSource&&IsWindowVisible(windows[1]));
            check("verify-auto-hide-first-touch-recovers-one-lost-artwork-submit",firstReveal&&revealSubmissions==2&&!windowError);
            report["revealSubmissions"]=revealSubmissions;report["revealError"]=windowError;
            const auto buttonChild=nativeButtonSurfaces[1].child();
            autoHideSetHidden(true);autoHideStopTimer();buttonUploads[1]={};
            revealFaultWindow=buttonOutlines[1];revealFaultsRemaining=1;revealSubmissions=0;
            windowError=ERROR_INVALID_STATE;
            handlePointer(windows[0],0,WM_POINTERDOWN,52,25,25,0);
            check("verify-special-button-reveal-retries-without-clearing-prior-error",buttonChild&&
                nativeButtonSurfaces[1].child()==buttonChild&&IsWindowVisible(buttonOutlines[1])&&
                revealSubmissions==2&&windowError==ERROR_INVALID_STATE);
            handlePointer(windows[0],0,WM_POINTERUP,52,25,25,0);
            windowError=0;autoHideSetHidden(true);autoHideStopTimer();outlineUploads[1]={};
            revealFaultWindow=outlines[1];revealFaultsRemaining=3;revealSubmissions=0;
            handlePointer(windows[0],0,WM_POINTERDOWN,53,25,25,0);
            const auto idleTimer=autoHide.timer;
            for(int i=0;i<16;++i)handlePointer(windows[0],0,WM_POINTERUPDATE,53,25.0f+i,25,0);
            handlePointer(windows[0],0,WM_POINTERUP,53,40,25,0);
            check("verify-permanent-artwork-fault-stops-after-one-retry",revealSubmissions==2&&
                revealFaultsRemaining==1&&state()["error"]==ERROR_RETRY&&!IsWindowVisible(outlines[1])&&
                IsWindowVisible(windows[1])&&nativeInputSurfaces[1].child()==rightSource&&idleTimer&&autoHide.timer==idleTimer);
            report["persistentRevealSubmissions"]=revealSubmissions;
            handlePointer(windows[0],0,WM_POINTERDOWN,153,25,25,0);
            handlePointer(windows[0],0,WM_POINTERUP,153,25,25,0);
            handlePointer(windows[0],0,WM_POINTERDOWN,154,25,25,0);
            handlePointer(windows[0],0,WM_POINTERUP,154,25,25,0);
            check("verify-next-down-spends-one-reveal-attempt-even-if-it-fails",revealSubmissions==3&&
                revealFaultsRemaining==0&&state()["error"]==ERROR_RETRY&&!IsWindowVisible(outlines[1])&&autoHide.timer==idleTimer&&
                nativeInputSurfaces[1].child()==rightSource);
            report["persistentRevealSubmissionsAfterNextDown"]=revealSubmissions;
            for(bool button:{false,true}){
                windowError=0;autoHideSetHidden(true);autoHideStopTimer();
                if(button)buttonUploads[1]={};else outlineUploads[1]={};
                const HWND visual=button?buttonOutlines[1]:outlines[1];
                const HWND source=button?nativeButtonSurfaces[1].child():nativeInputSurfaces[1].child();
                revealFaultWindow=visual;revealFaultsRemaining=2;revealSubmissions=0;
                handlePointer(windows[0],0,WM_POINTERDOWN,155,25,25,0);
                handlePointer(windows[0],0,WM_POINTERUP,155,25,25,0);
                const auto timer=autoHide.timer;
                const bool firstAttemptsBounded=revealSubmissions==2&&!IsWindowVisible(visual);
                const auto failedReveal=state();
                check(button?"verify-button-reveal-error-not-recorded-as-window-fault":"verify-pad-reveal-error-not-recorded-as-window-fault",
                    failedReveal["error"]==ERROR_RETRY&&failedReveal["ok"]==false&&windowError==0&&!failedReveal.contains("nativeInputFailure"));
                if(button)windowError=ERROR_INVALID_STATE; // A newer, unrelated error must remain.
                if(button){
                    RECT rc{};GetClientRect(buttonWindows[2],&rc);
                    handleButtonPointer(buttonWindows[2],2,WM_POINTERDOWN,156,rc.right*.5f,rc.bottom*.5f);
                    handleButtonPointer(buttonWindows[2],2,WM_POINTERUP,156,rc.right*.5f,rc.bottom*.5f);
                }else{
                    handlePointer(windows[0],0,WM_POINTERDOWN,156,25,25,0);
                    handlePointer(windows[0],0,WM_POINTERUP,156,25,25,0);
                }
                check(button?"verify-next-down-restores-only-missing-button-artwork":"verify-next-down-restores-only-missing-pad-artwork",
                    firstAttemptsBounded&&IsWindowVisible(visual)&&revealSubmissions==3&&timer&&autoHide.timer==timer&&
                    source==(button?nativeButtonSurfaces[1].child():nativeInputSurfaces[1].child()));
                if(button){
                    const auto retained=state();
                    check("verify-button-recovery-preserves-unrelated-window-error",windowError==ERROR_INVALID_STATE&&retained["error"]==ERROR_INVALID_STATE&&retained["ok"]==false);
                    windowError=0;
                }
                const auto recovered=state();
                check(button?"verify-button-recovery-removes-own-error":"verify-pad-recovery-removes-own-error",recovered["error"]==0&&recovered["ok"]==true);
            }
            windowError=0;autoHideSetHidden(true);autoHideStopTimer();outlineUploads[1]={};buttonUploads[1]={};
            revealFaultWindow=outlines[1];revealFaultsRemaining=2;revealSubmissions=0;
            secondRevealFaultWindow=buttonOutlines[1];secondRevealFaultsRemaining=3;secondRevealSubmissions=0;
            handlePointer(windows[0],0,WM_POINTERDOWN,159,25,25,0);
            handlePointer(windows[0],0,WM_POINTERUP,159,25,25,0);
            const bool bothMissing=revealSubmissions==2&&secondRevealSubmissions==2&&
                !IsWindowVisible(outlines[1])&&!IsWindowVisible(buttonOutlines[1]);
            handlePointer(windows[0],0,WM_POINTERDOWN,160,25,25,0);
            handlePointer(windows[0],0,WM_POINTERUP,160,25,25,0);
            const auto partial=state();
            check("verify-recovering-one-outline-keeps-the-other-outlines-error",bothMissing&&revealSubmissions==3&&secondRevealSubmissions==3&&
                IsWindowVisible(outlines[1])&&!IsWindowVisible(buttonOutlines[1])&&partial["error"]==ERROR_BUSY&&partial["ok"]==false&&windowError==0);
            const auto previousInputError=inputError;inputError=ERROR_INVALID_HANDLE;
            check("verify-input-error-takes-precedence-over-artwork-error",state()["error"]==ERROR_INVALID_HANDLE);
            inputError=previousInputError;secondRevealFaultWindow=nullptr;
            windowError=0;autoHideSetHidden(true);autoHideStopTimer();outlineUploads[1]={};
            revealFaultWindow=outlines[1];revealFaultsRemaining=2;revealSubmissions=0;
            handlePointer(windows[0],0,WM_POINTERDOWN,157,25,25,0);
            handlePointer(windows[0],0,WM_POINTERUP,157,25,25,0);
            revealFaultsRemaining=1;revealSubmissionsAtAction=revealSubmissionsAfterAction=0;
            revealFaultAction=[](){auto off=getConfig();off.masterEnabled=false;configure(off,1,true);};
            handlePointer(windows[0],0,WM_POINTERDOWN,158,25,25,0);
            report["offDuringNextDown"]={{"actionCalled",revealFaultAction==nullptr},{"atOffStart",revealSubmissionsAtAction},{"returnSubmissions",revealSubmissionsAfterAction},
                {"finalSubmissions",revealSubmissions},{"masterEnabled",getConfig().masterEnabled},{"input",!!IsWindowVisible(windows[1])},
                {"artwork",!!IsWindowVisible(outlines[1])},{"timer",autoHide.timer},{"error",windowError}};
            check("verify-OFF-during-next-down-repair-cannot-reshow-or-arm-timer",!revealFaultAction&&
                revealSubmissionsAtAction==3&&revealSubmissions==revealSubmissionsAfterAction&&!getConfig().masterEnabled&&
                !IsWindowVisible(windows[1])&&!IsWindowVisible(outlines[1])&&!autoHide.timer&&!windowError);
            revealFaultAction=nullptr;revealFaultsRemaining=0;
            revealFaultWindow=nullptr;configure(cfg,1,true);
            for(int i=0;i<3;++i){
                if(i)configure(cfg,1,true);
                windowError=0;autoHideSetHidden(true);autoHideStopTimer();
                const bool settings=i==2,button=i==1;
                if(settings)outlineUploads[1].pressed=!surfacePressed(1); // Force a paint without invalidating the source.
                else if(button)buttonUploads[1]={};else outlineUploads[1]={};
                const HWND input=button?buttonWindows[1]:windows[1],art=button?buttonOutlines[1]:outlines[1];
                revealFaultWindow=art;revealFaultsRemaining=1;revealSubmissions=0;revealSubmissionsAfterAction=0;
                revealFaultAction=[](){auto off=getConfig();off.masterEnabled=false;configure(off,1,true);};
                bool fullRefresh=false;
                if(settings)fullRefresh=configure(cfg,1,true);
                else handlePointer(windows[0],0,WM_POINTERDOWN,54+i,25,25,0);
                check(settings?"verify-superseded-settings-preview-does-not-arm-idle-timer":button?
                    "verify-superseded-reveal-cannot-resurrect-off-button":"verify-superseded-reveal-cannot-resurrect-off-artwork",
                    !revealFaultAction&&revealSubmissionsAfterAction&&revealSubmissions==revealSubmissionsAfterAction&&
                    !getConfig().masterEnabled&&!IsWindowVisible(input)&&!IsWindowVisible(art)&&!autoHide.timer&&!windowError&&!fullRefresh);
                report[settings?"offDuringSettingsReveal":button?"offDuringButtonReveal":"offDuringReveal"]={{"submissions",revealSubmissions},
                    {"submissionsAtOffReturn",revealSubmissionsAfterAction},{"artworkVisible",!!IsWindowVisible(art)},
                    {"inputVisible",!!IsWindowVisible(input)},{"error",windowError},{"timerArmed",autoHide.timer!=0},
                    {"fullRefresh",fullRefresh}};
                revealFaultWindow=nullptr;
            }
            revealFaultWindow=nullptr;
            const auto old=ownerResumeRecovery;cfg.masterEnabled=false;configure(cfg,1,true);ownerResumeTimerProc(old.window,WM_TIMER,old.timer,0);
            check("verify-OFF-with-delayed-probe-cannot-revive-input",!buttonsVisible()&&!IsWindowVisible(windows[0])&&!ownerResumeRecovery.pending);
            ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;report["selector"]="--switch-storm-autohide";
        }else if(verificationOnly){
            cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;cfg.touchFeedback=TouchFeedbackMode::Off;
            setContext("steamdeck",true);configure(cfg,1,true);pump();
            stopOwnerVisibilityRechecks();ownerVisibilityQueued=false;ownerVisibilityRaiseRequested=false;ownerVisibilityArtworkRequested=false;
            const auto source=nativeInputSurfaces[0].child();const auto uploads=outlineUploadCount;const auto geometry=padRefreshEpoch;
            const bool rebound=configure(cfg,1,true);
            check("verify-manual-reselect-queues-artwork-without-input-rebind",!rebound&&padRefreshEpoch==geometry&&source==nativeInputSurfaces[0].child()&&ownerVisibilityQueued&&ownerVisibilityArtworkRequested);
#ifdef YMCC_STORM_OLD_CONTROL
            if(!passed)throw std::runtime_error("old-control-necessary-missed-manual-reshow");
#endif
            // On a hidden desktop foreground can be null; direct artwork sink is
            // independently covered by previous native raw, not inferred here.
            report["manualRequestQueued"]=ownerVisibilityArtworkRequested;report["manualUploadsBefore"]=uploads;pump();
            static ymcc::sleeppower::SharedPowerV1 observed;observed={};observed.generation=610;observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
            ownerPowerCapture=[](ymcc::sleeppower::SharedPowerV1& out){out=observed;return ymcc::sleeppower::powerAdmitted(out);};
            ownerPowerCurrent=[](const ymcc::sleeppower::SharedPowerV1& p){return ymcc::sleeppower::samePowerRevocation(p,observed);};
            const auto tick=[](){const auto r=ownerResumeRecovery;if(r.timer)ownerResumeTimerProc(r.window,WM_TIMER,r.timer,0);};
            ownerOnSuspend();queueOwnerResumeRebind();pump();ownerResumeReady(observed.generation);tick();tick();
            check("verify-two-probes-are-clean-with-zero-repairs",ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==0);
            cfg.autoHideSeconds=15;configure(cfg,1,true);autoHideSetHidden(true);const auto hiddenUploads=outlineUploadCount;ownerOnSuspend();queueOwnerResumeRebind();pump();++observed.generation;ownerResumeReady(observed.generation);tick();tick();
            check("verify-auto-hidden-source-health-does-not-reshow-artwork",ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&!IsWindowVisible(outlines[0])&&autoHide.hidden);
            if(!passed)throw std::runtime_error("verification-failure-no-later-business");
            report["hiddenUploadsBefore"]=hiddenUploads;autoHideSetHidden(false);
            {
                const auto startProbe=[&](bool hidden){
                    autoHideSetHidden(hidden);autoHideStopTimer();
                    ownerOnSuspend();queueOwnerResumeRebind();pump();autoHideStopTimer();
                    ownerResumeReady(++observed.generation);tick();
                    return ownerResumeRecovery.pending&&ownerResumeRecovery.clean==1&&autoHide.hidden==hidden;
                };
                const bool firstHidden=startProbe(true);
                const auto revealDeadline=ownerResumeRecovery.deadline;
                const auto revealPadEpoch=padRefreshEpoch,revealButtonEpoch=buttonRefreshEpoch;
                autoHideSetHidden(false);const auto afterReveal=outlineUploadCount;tick();
                check("verify-reveal-between-wake-checks-reprobes-artwork-within-original-deadline",firstHidden&&
                    !autoHide.hidden&&IsWindowVisible(outlines[0])&&outlineUploadCount>afterReveal&&
                    padRefreshEpoch==revealPadEpoch&&buttonRefreshEpoch==revealButtonEpoch&&
                    ownerResumeRecovery.pending&&ownerResumeRecovery.clean==1&&ownerResumeRecovery.repairs==0&&
                    ownerResumeRecovery.deadline==revealDeadline);
                tick();
                check("verify-revealed-wake-checks-settle-without-repair",!ownerResumeRecovery.pending&&
                    ownerResumeRecovery.clean==2&&ownerResumeRecovery.repairs==0);

                const bool firstVisible=startProbe(false);
                const auto inProbeDeadline=ownerResumeRecovery.deadline,serial=autoHide.revealSerial;
                bool flipped=false;
                auto flip=nativeInputSurfaces[0].source().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{
                    autoHideSetHidden(true);autoHideSetHidden(false);flipped=true;
                });tick();
                check("verify-hide-reveal-during-wake-probe-cannot-reuse-old-clean-result",firstVisible&&flipped&&
                    !autoHide.hidden&&autoHide.revealSerial!=serial&&ownerResumeRecovery.pending&&
                    ownerResumeRecovery.clean==0&&ownerResumeRecovery.repairs==0&&ownerResumeRecovery.deadline==inProbeDeadline);
                tick();tick();
                check("verify-fresh-wake-probes-settle-after-reentrant-reveal",!ownerResumeRecovery.pending&&
                    ownerResumeRecovery.clean==2&&ownerResumeRecovery.repairs==0);
                if(!passed)throw std::runtime_error("wake-reveal-visibility-epoch-failure");
            }
            {
                const auto startProbe=[&]{
                    ownerOnSuspend();queueOwnerResumeRebind();pump();ownerResumeReady(++observed.generation);tick();
                    return ownerResumeRecovery.pending&&ownerResumeRecovery.clean==1;
                };
                const auto recommit=[] {
                    observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
                    ++observed.publicationRevision;ownerSharedPowerChanged(observed);
                };
                const auto revoke=[] {
                    ++observed.observedNotificationSequence;
                    observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kNotificationPending;
                };
                const bool caughtBeforeReady=startProbe();
                const auto before=ownerResumeRecovery;
                const auto source=nativeInputSurfaces[0].child();
                revoke();tick();
                check("verify-revoked-wake-check-does-not-run-or-poll-while-not-ready",caughtBeforeReady&&
                    !ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.checks==before.checks);
                recommit();
                const auto accepted=ownerResumeRecovery;
                ownerResumeTimerProc(before.window,WM_TIMER,before.timer,0);
                check("verify-ready-recommit-continues-original-wake-without-rebind-or-deadline-reset",accepted.pending&&
                    accepted.timer&&accepted.timer!=before.timer&&ownerResumeRecovery.timer==accepted.timer&&
                    accepted.deadline==before.deadline&&accepted.clean==0&&accepted.checks==before.checks&&
                    accepted.repairs==before.repairs&&nativeInputSurfaces[0].child()==source&&!ownerRebindQueued);
                tick();tick();
                check("verify-recommitted-wake-requires-two-new-clean-checks",ownerResumeRecovery.clean==2&&
                    !ownerResumeRecovery.pending&&ownerResumeRecovery.checks==before.checks+2&&ownerResumeRecovery.repairs==0);

                const bool caughtInsideProbe=startProbe();const auto old=ownerResumeRecovery;
                bool dispatched=false;
                auto action=nativeInputSurfaces[0].source().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{
                    dispatched=true;revoke();recommit();
                });tick();
                check("verify-nested-ready-recommit-fences-the-old-wake-callback",caughtInsideProbe&&dispatched&&
                    ownerResumeRecovery.pending&&ownerResumeRecovery.timer!=old.timer&&ownerResumeRecovery.clean==0&&
                    ownerResumeRecovery.deadline==old.deadline&&ownerResumeRecovery.repairs==0);
                tick();tick();
                check("verify-nested-recommit-settles-without-charging-a-repair",!ownerResumeRecovery.pending&&
                    ownerResumeRecovery.clean==2&&ownerResumeRecovery.repairs==0);

                const bool reachesExpiry=startProbe();revoke();tick();
                ownerResumeRecovery.deadline=GetTickCount64();recommit();
                check("verify-ready-recommit-cannot-reopen-an-expired-wake",reachesExpiry&&!ownerResumeRecovery.pending&&
                    !ownerResumeRecovery.timer&&std::string(ownerResumeRecovery.status)=="expired"&&ownerResumeRecovery.repairs==0);
                if(!passed)throw std::runtime_error("ready-recommit-wake-failure");
                // Retain a healthy last check for the following late-fault case.
                startProbe();tick();
            }
            // A closed owned compositor must not inherit a previous healthy receipt.
            release();for(int side=0;side<2;++side)nativeInputClose(side);buttonsNativeInputDisable();nativePadInputRuntime.compositor.Close();refresh(true);
            check("verify-late-closed-runtime-starts-after-successful-verification",windowError&&ownerResumeRecovery.pending);
            tick();tick();tick();tick();
            check("verify-real-closed-runtime-recovers-through-owned-rebuild",ownerResumeControlsHealthy()&&!ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==2&&ownerResumeRecovery.runtimeRebuilds==1&&ownerResumeRecovery.clean==2);
            check("verify-budget-tracks-actual-repairs-not-healthy-checks",ownerResumeRecoveryState()["faultBudget"]["repairs"]==2);
            // An old queued source may call configuration synchronously while
            // health is draining it. The replaced source cannot confirm health.
            ownerOnSuspend();queueOwnerResumeRebind();pump();++observed.generation;ownerResumeReady(observed.generation);
            const auto originalDeadline=ownerResumeRecovery.deadline;
            bool changed=false;auto action=nativeInputSurfaces[0].source().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{changed=true;auto next=getConfig();next.scale+=1;configure(next,1,true);});tick();
            check("verify-configuration-reentry-cannot-confirm-replaced-sources",changed&&ownerResumeRecovery.clean==0&&std::string(ownerResumeRecovery.status)!="healthy"&&std::string(ownerResumeRecovery.status)!="recovered");
            check("verify-replaced-source-does-not-charge-repair-or-renew-deadline",ownerResumeRecovery.pending&&ownerResumeRecovery.timer&&ownerResumeRecovery.deadline==originalDeadline&&ownerResumeRecovery.repairs==0&&ownerFaultBudget.repairs==0);
            tick();
            const auto firstClean=ownerResumeRecovery.clean;const auto firstCleanEpoch=padRefreshEpoch;
            auto replacement=getConfig();++replacement.scale;configure(replacement,1,true);tick();
            check("verify-clean-probes-cannot-be-combined-across-refresh",firstClean==1&&padRefreshEpoch!=firstCleanEpoch&&ownerResumeRecovery.pending&&ownerResumeRecovery.clean==1&&ownerResumeRecovery.deadline==originalDeadline&&ownerResumeRecovery.repairs==0);
            tick();
            check("verify-replaced-source-rechecked-twice-without-repair",ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==0);
            ownerOnSuspend();queueOwnerResumeRebind();pump();++observed.generation;ownerResumeReady(observed.generation);
            const auto displayDeadline=ownerResumeRecovery.deadline;
            bool deferred=false;auto displayAction=nativeInputSurfaces[0].source().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{deferred=queueOwnerDisplayRefresh(true);});tick();
            check("verify-display-reentry-withdraws-without-charging-repair",deferred&&ownerDisplayRefresh.pending&&ownerResumeRecovery.clean==0&&ownerResumeRecovery.repairs==0&&ownerFaultBudget.repairs==0);
            tick();
            check("verify-deferred-display-preserves-original-recovery-deadline",ownerResumeRecovery.pending&&ownerResumeRecovery.timer&&ownerResumeRecovery.deadline==displayDeadline&&ownerResumeRecovery.repairs==0);
            const auto transition=ownerDisplayRefresh;ownerDisplayRefreshTimerProc(transition.window,WM_TIMER,transition.timer,0);
            tick();tick();
            check("verify-display-reflow-rechecked-twice-without-repair",!ownerDisplayRefresh.pending&&ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&ownerResumeRecovery.repairs==0);
            {
                namespace custom=ymcc::customtouch;
                custom::resolveKey=[](const std::string&,const std::string&,WORD& vk,bool& shift){vk=VK_INSERT;shift=false;return true;};
                custom::initialize(custom::defaults());pump();
                report["repairCloseReentry"]=Json::array();
                for(unsigned stage:{1u,2u,3u}){
                    // Separate awake epochs. Each earlier repair uses the real
                    // close path before we inject reflow into the chosen stage.
                    refresh(true);pump();ownerOnSuspend();queueOwnerResumeRebind();pump();ownerResumeReady(++observed.generation);
                    for(unsigned prior=1;prior<stage;++prior){windowError=ERROR_NOT_READY;tick();}
                    if(custom::items.empty()||!custom::items.front()->surface.active()||!ownerResumeRecovery.timer||ownerResumeRecovery.repairs!=stage-1)
                        throw std::runtime_error("repair-close fixture missing active custom input, timer or earlier repair");
                    const auto deadline=ownerResumeRecovery.deadline;
                    const auto originalTimer=ownerResumeRecovery.timer;
                    const auto stageName="verify-repair-close-stage-"+std::to_string(stage);
                    const HWND customRoot=custom::items.front()->window;
                    previousRepairCloseProbeProc=(WNDPROC)SetWindowLongPtrW(customRoot,GWLP_WNDPROC,(LONG_PTR)reflowOnRepairClose);
                    if(!previousRepairCloseProbeProc)throw std::runtime_error("repair-close fixture subclass failed");
                    repairCloseReentry=false;repairCloseArmed=true;
                    windowError=ERROR_NOT_READY; // Admit repair without breaking a real source.
                    tick();
                    repairCloseArmed=false;
                    if(IsWindow(customRoot))SetWindowLongPtrW(customRoot,GWLP_WNDPROC,(LONG_PTR)previousRepairCloseProbeProc);
                    const auto pending=ownerResumeRecoveryState();
                    const auto reflow=ownerDisplayRefresh;
                    check(stageName+"-retains-bounded-probe-with-no-channel-reset",
                        repairCloseReentry&&reflow.pending&&ownerResumeRecovery.pending&&ownerResumeRecovery.timer&&
                        ownerResumeRecovery.timer!=originalTimer&&ownerResumeRecovery.deadline==deadline&&
                        ownerResumeRecovery.repairs==stage&&ownerFaultBudget.repairs==stage&&ownerResumeRecovery.channelResets==0);
                    if(reflow.timer)ownerDisplayRefreshTimerProc(reflow.window,WM_TIMER,reflow.timer,0);
                    const bool ready=ownerResumeControlsHealthy()&&!custom::items.empty()&&!custom::items.front()->retiring;
                    tick();tick();
                    check(stageName+"-successor-reflow-revalidated-twice",
                        ready&&!ownerDisplayRefresh.pending&&!ownerResumeRecovery.pending&&ownerResumeRecovery.clean==2&&
                        ownerResumeRecovery.repairs==stage&&ownerResumeControlsHealthy());
                    report["repairCloseReentry"].push_back({{"stage",stage},{"entered",repairCloseReentry},{"reflowPending",reflow.pending},
                        {"successorReady",ready},{"afterClose",pending},{"afterVerification",ownerResumeRecoveryState()}});
                    cancelOwnerResumeRecovery("fixture-stage-end");
                }
                if(!passed)throw std::runtime_error("repair-close verification failed");
                for(const bool rejectResumeTimer:{false,true}){
                    const std::string label=rejectResumeTimer?"verify-editor-rejected-reflow":"verify-editor-deferred-reflow";
                    auto active=getConfig();active.autoHideSeconds=15;configure(active,1,true);pump();
                    const auto completeBefore=ownerDisplayRefresh.completed;
                    const auto rejectedBefore=reflowTimerRejections;
                    const bool queued=queueOwnerDisplayRefresh(true);
                    const auto initial=ownerDisplayRefresh;
                    const bool entered=custom::begin();
                    if(initial.timer)ownerDisplayRefreshTimerProc(initial.window,WM_TIMER,initial.timer,0);
                    check(label+"-editor-holds-withdrawn-reflow",queued&&entered&&custom::editing.load()&&
                        ownerDisplayReflowInterrupted&&displayTransitionFence.idleSuspended&&!ownerDisplayRefresh.pending);
                    check(label+"-phase-identifies-editor",ownerDisplayExportState()["phase"]=="waiting-for-editor-exit");
                    rejectNextReflowTimer=rejectResumeTimer;
                    const bool closed=custom::finish(false,false);
                    rejectNextReflowTimer=false;
                    const auto rescheduled=ownerDisplayRefresh;
                    if(rescheduled.timer)ownerDisplayRefreshTimerProc(rescheduled.window,WM_TIMER,rescheduled.timer,0);
                    check(label+"-close-restores-controls-and-idle-without-power-event",
                        closed&&!custom::editing.load()&&!ownerDisplayReflowInterrupted&&!displayTransitionFence.idleSuspended&&
                        !ownerDisplayRefresh.pending&&ownerDisplayRefresh.completed==completeBefore+1&&ownerResumeControlsHealthy()&&
                        autoHide.timer&&reflowTimerRejections==rejectedBefore+(rejectResumeTimer?1:0));
                    // Clean up any stranded old-code reproduction before running the other case.
                    if(ownerDisplayReflowInterrupted&&queueOwnerDisplayRefresh(true,true)){
                        const auto cleanup=ownerDisplayRefresh;
                        if(cleanup.timer)ownerDisplayRefreshTimerProc(cleanup.window,WM_TIMER,cleanup.timer,0);
                    }
                }
                if(!passed)throw std::runtime_error("editor display handoff verification failed");
                {
                    const bool queued=queueOwnerDisplayRefresh(true);
                    const auto initial=ownerDisplayRefresh;
                    const bool entered=custom::begin();
                    if(initial.timer)ownerDisplayRefreshTimerProc(initial.window,WM_TIMER,initial.timer,0);
                    const auto flags=observed.flags;
                    inputPowerAllowed=[] {return ymcc::sleeppower::powerAdmitted(observed);};
                    observed.flags=ymcc::sleeppower::kKnown;
                    const bool closed=custom::finish(false,false);
                    check("verify-editor-close-keeps-input-hidden-during-power-block",queued&&entered&&closed&&
                        ownerDisplayReflowInterrupted&&displayTransitionFence.idleSuspended&&!ownerDisplayRefresh.pending&&
                        !visibleScreenControls()&&ownerDisplayExportState()["phase"]=="waiting-for-power-ready");
                    observed.flags=flags;ownerSharedPowerChanged(observed);
                    const auto resumed=ownerDisplayRefresh;
                    if(resumed.timer)ownerDisplayRefreshTimerProc(resumed.window,WM_TIMER,resumed.timer,0);
                    check("verify-editor-close-interrupted-by-power-restores-only-on-ready",!ownerDisplayReflowInterrupted&&
                        !displayTransitionFence.idleSuspended&&!ownerDisplayRefresh.pending&&ownerResumeControlsHealthy()&&autoHide.timer);
                    inputPowerAllowed=nullptr;
                }
                auto off=custom::defaults();off["enabled"]=false;custom::configure(off);pump();custom::resolveKey=nullptr;
            }
            for(const bool hideArtwork:{false,true}){
                ownerOnSuspend();queueOwnerResumeRebind();pump();++observed.generation;ownerResumeReady(observed.generation);tick();
                const bool firstClean=ownerResumeRecovery.pending&&ownerResumeRecovery.clean==1;
                bool crossed=false;auto expiredAction=nativeInputSurfaces[0].source().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{
                    crossed=true;
                    // Move the deadline inside the callback instead of sleeping the STA.
                    ownerResumeRecovery.deadline=GetTickCount64();
                    if(hideArtwork)ShowWindow(outlines[0],SW_HIDE);
                });tick();
                check(hideArtwork?"verify-expired-probe-cannot-start-repair":"verify-expired-probe-cannot-certify-health",
                    firstClean&&crossed&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&std::string(ownerResumeRecovery.status)=="expired"&&
                    ownerResumeRecovery.clean<2&&ownerResumeRecovery.repairs==0&&ownerFaultBudget.repairs==0);
            }
            configure(getConfig(),1,true);pump();
            const HWND hooked=windows[0],otherPad=nativeInputSurfaces[1].child(),button=nativeButtonSurfaces[1].child();
            if(!hooked||!otherPad||!button)throw std::runtime_error("old-rebind-probe-surfaces-missing");
            ownerOnSuspend();const auto staleRebindEpoch=ownerLifecycleEpoch;
            if(!queueOwnerResumeRebind())throw std::runtime_error("old-rebind-probe-not-queued");
            suspendEnteredDuringRebind=false;
            previousRebindProbeProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(hooked,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(suspendOnOldRebindClose)));
            if(!previousRebindProbeProc)throw std::runtime_error("old-rebind-probe-subclass-failed");
            const auto padEpochBeforeClose=padRefreshEpoch;
            const bool oldRebindCompleted=consumeOwnerRebind(staleRebindEpoch);
            if(IsWindow(hooked))SetWindowLongPtrW(hooked,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previousRebindProbeProc));
            check("old-rebind-close-dispatches-real-nested-suspend",suspendEnteredDuringRebind&&ownerLifecycleEpoch!=staleRebindEpoch);
            check("old-rebind-cannot-retire-other-sources-or-refresh-after-nested-suspend",!oldRebindCompleted&&padRefreshEpoch==padEpochBeforeClose+1&&
                nativeInputSurfaces[1].child()==otherPad&&nativeButtonSurfaces[1].child()==button&&IsWindow(otherPad)&&IsWindow(button));
            const bool freshRebind=queueOwnerResumeRebind();pump();
            check("next-rebind-restores-sources-after-nested-suspend",freshRebind&&!ownerRebindQueued&&ownerResumeControlsHealthy());
            ownerSharedPowerChanged(observed);
            inputPowerAllowed=[] {return ymcc::sleeppower::powerAdmitted(observed);};
            const auto beforeSuspendReflow=ownerDisplayRefresh.completed;
            const bool queuedForSuspend=queueOwnerDisplayRefresh(true);
            const auto oldReflow=ownerDisplayRefresh;bool suspendedInReflow=false;
            const auto timerPendingReceipt=ownerDisplayExportState();Json refreshingReceipt;
            auto suspendAction=nativeInputSurfaces[0].source().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{
                refreshingReceipt=ownerDisplayExportState();
                suspendedInReflow=true;observed.flags=ymcc::sleeppower::kKnown;++observed.suspendSerial;ownerSharedPowerChanged(observed);
            });
            ownerDisplayRefreshTimerProc(oldReflow.window,WM_TIMER,oldReflow.timer,0);
            const auto waitingReceipt=std::async(std::launch::async,ownerDisplayExportState).get();
            report["displayExport"]={{"pending",timerPendingReceipt},{"refreshing",refreshingReceipt},{"waiting",waitingReceipt}};
            check("verify-reflow-export-distinguishes-pending-refreshing-and-power-wait",timerPendingReceipt["phase"]=="timer-pending"&&
                refreshingReceipt["phase"]=="refreshing"&&waitingReceipt["phase"]=="waiting-for-power-ready"&&waitingReceipt["phaseError"]==0);
            check("verify-suspend-cancels-in-flight-display-reflow",queuedForSuspend&&suspendedInReflow&&ownerPowerWithdrawn&&
                !ownerDisplayRefresh.pending&&!IsWindowVisible(windows[0])&&!IsWindowVisible(outlines[0])&&
                !IsWindowVisible(windows[1])&&!IsWindowVisible(outlines[1]));
            check("verify-canceled-display-reflow-cannot-publish-completion",ownerDisplayRefresh.completed==beforeSuspendReflow&&!ownerResumeRecovery.pending);
            ++observed.generation;observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
            ownerSharedPowerChanged(observed);pump();
            const auto resumedReflow=ownerDisplayRefresh;
            check("verify-ready-reschedules-canceled-display-work",resumedReflow.pending&&resumedReflow.timer&&resumedReflow.timer!=oldReflow.timer);
            if(resumedReflow.timer)ownerDisplayRefreshTimerProc(resumedReflow.window,WM_TIMER,resumedReflow.timer,0);
            tick();tick();tick();tick();
            check("verify-ready-revalidates-after-canceled-display-reflow",ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&ownerResumeControlsHealthy());
            check("verify-ready-finishes-display-work-without-consuming-repair",ownerDisplayRefresh.completed==beforeSuspendReflow+1&&ownerFaultBudget.repairs==0&&ownerResumeRecovery.repairs==0);
            for(const bool publishSuspend:{true,false}){
                const std::string label=publishSuspend?"verify-timer-before-suspend-projection":"verify-timer-with-coalesced-admission";
                const auto beforeRace=ownerDisplayRefresh.completed;
                const bool queuedRace=queueOwnerDisplayRefresh(true);const auto old=ownerDisplayRefresh;
                observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kNotificationPending;
                if(publishSuspend)++observed.suspendSerial;
                ownerDisplayRefreshTimerProc(old.window,WM_TIMER,old.timer,0);
                check(label+"-non-ready-does-not-reshow",queuedRace&&!ownerDisplayRefresh.pending&&ownerDisplayRefresh.completed==beforeRace&&
                    !IsWindowVisible(windows[0])&&!IsWindowVisible(windows[1]));
                if(publishSuspend)ownerSharedPowerChanged(observed);
                if(publishSuspend)++observed.generation;
                observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
                ownerSharedPowerChanged(observed);
                const auto next=ownerDisplayRefresh;
                check(label+"-ready-reschedules-withdrawn-display",next.pending&&next.timer&&next.timer!=old.timer);
                if(next.timer)ownerDisplayRefreshTimerProc(next.window,WM_TIMER,next.timer,0);
                const auto repairs=ownerFaultBudget.repairs;tick();tick();tick();tick();
                check(label+"-both-pads-return-without-repair",ownerDisplayRefresh.completed==beforeRace+1&&ownerResumeControlsHealthy()&&
                    IsWindowVisible(windows[0])&&IsWindowVisible(windows[1])&&ownerFaultBudget.repairs==0&&repairs==0);
                // Preserve the independent second order even if the old code fails the first.
                if(!ownerResumeControlsHealthy())configure(getConfig(),1,true);
                pump();
            }
            for(const bool timerFirst:{false,true}){
                const std::string label=timerFirst?"verify-idle-timer-before-suspend":"verify-idle-suspend-before-timer";
                auto idle=getConfig();idle.autoHideSeconds=15;configure(idle,1,true);pump();
                autoHideSetHidden(true);autoHideStopTimer();const auto lastTouch=autoHide.lastTouch,wakes=autoHide.wakes;
                check(label+"-starts-hidden-with-live-input",autoHide.hidden&&IsWindowVisible(windows[0])&&!IsWindowVisible(outlines[0]));
                const bool queued=queueOwnerDisplayRefresh(true);const auto prior=ownerDisplayRefresh;
                observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kNotificationPending;++observed.suspendSerial;
                if(timerFirst)ownerDisplayRefreshTimerProc(prior.window,WM_TIMER,prior.timer,0);
                ownerSharedPowerChanged(observed);
                if(!timerFirst)ownerDisplayRefreshTimerProc(prior.window,WM_TIMER,prior.timer,0);
                check(label+"-withdrawal-does-not-clear-idle",queued&&autoHide.hidden&&autoHide.lastTouch==lastTouch&&autoHide.wakes==wakes);
                ++observed.generation;observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
                ownerSharedPowerChanged(observed);
                const auto next=ownerDisplayRefresh;if(next.timer)ownerDisplayRefreshTimerProc(next.window,WM_TIMER,next.timer,0);
                tick();tick();tick();tick();
                check(label+"-ready-restores-input-without-revealing-artwork",ownerResumeControlsHealthy()&&autoHide.hidden&&autoHide.lastTouch==lastTouch&&autoHide.wakes==wakes&&
                    IsWindowVisible(windows[0])&&IsWindowVisible(windows[1])&&!IsWindowVisible(outlines[0])&&!IsWindowVisible(outlines[1]));
                RECT panel{};GetClientRect(windows[0],&panel);
                ymcc::screeninput::Event touch{0,813,PT_TOUCH,ymcc::screeninput::Phase::Down,panel.right*.5f,panel.bottom*.5f};
                nativeInputSink(windows[0],touch,nullptr);
                check(label+"-next-touch-reveals-without-losing-down",core.pads[0].down&&!autoHide.hidden&&autoHide.wakes==wakes+1&&IsWindowVisible(outlines[0]));
                touch.phase=ymcc::screeninput::Phase::Up;nativeInputSink(windows[0],touch,nullptr);pump();
            }
            const auto completedBeforeReject=ownerDisplayRefresh.completed;
            const auto rejectCount=reflowTimerRejections;
            const bool normalReflow=queueOwnerDisplayRefresh(true);const auto active=ownerDisplayRefresh;
            bool nestedRejected=false;
            auto rejectingAction=nativeInputSurfaces[0].source().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{
                rejectNextReflowTimer=true;nestedRejected=!queueOwnerDisplayRefresh(true)&&reflowTimerRejections==rejectCount+1;
            });
            if(active.timer)ownerDisplayRefreshTimerProc(active.window,WM_TIMER,active.timer,0);
            check("verify-rejected-nested-timer-keeps-older-reflow-authority",normalReflow&&nestedRejected&&ownerDisplayRefresh.completed==completedBeforeReject+1&&
                !ownerDisplayRefresh.pending&&!displayTransitionFence.idleSuspended&&ownerResumeControlsHealthy());
            if(!ownerResumeControlsHealthy())configure(getConfig(),1,true);
            pump();
            autoHideSetHidden(true);autoHideStopTimer();const auto dormantSince=autoHide.lastTouch;
            const auto beforeRejectedWake=ownerDisplayRefresh.completed;
            const bool queuedBeforeRejectedWake=queueOwnerDisplayRefresh(true);
            observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kNotificationPending;++observed.suspendSerial;
            ownerSharedPowerChanged(observed);
            ++observed.generation;observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
            rejectNextReflowTimer=true;const auto rejectedBeforeReady=reflowTimerRejections;ownerSharedPowerChanged(observed);
            check("verify-rejected-ready-timer-still-restores-withdrawn-input",queuedBeforeRejectedWake&&reflowTimerRejections==rejectedBeforeReady+1&&
                !ownerDisplayRefresh.pending&&ownerDisplayRefresh.completed==beforeRejectedWake+1&&ownerResumeControlsHealthy()&&
                !ownerDisplayReflowInterrupted&&IsWindowVisible(windows[0])&&IsWindowVisible(windows[1]));
            check("verify-ready-timer-fallback-keeps-idle-artwork-hidden",autoHide.hidden&&autoHide.lastTouch==dormantSince&&!IsWindowVisible(outlines[0])&&!IsWindowVisible(outlines[1]));
            tick();tick();tick();tick();
            check("verify-ready-timer-fallback-does-not-charge-repair",ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&ownerFaultBudget.repairs==0);
            const auto fallbackReceipt=std::async(std::launch::async,ownerDisplayExportState).get();
            report["displayExport"]["fallbackSettled"]=fallbackReceipt;
            check("verify-reflow-export-keeps-timer-error-after-successful-fallback",fallbackReceipt["phase"]=="settled"&&
                fallbackReceipt["phaseError"]==0&&fallbackReceipt["lastTimerError"]==ERROR_NOT_ENOUGH_MEMORY);
            if(!ownerResumeControlsHealthy())configure(getConfig(),1,true);
            pump();
            for(const bool readyFallback:{false,true}){
                const std::string label=readyFallback?"verify-admission-during-ready-fallback":"verify-admission-during-reflow-timer";
                autoHideSetHidden(true);autoHideStopTimer();const auto idleStart=autoHide.lastTouch;
                const auto completed=ownerDisplayRefresh.completed;const auto rejected=reflowTimerRejections;
                const bool queued=queueOwnerDisplayRefresh(true);const auto old=ownerDisplayRefresh;
                const HWND mid=windows[0];
                if(readyFallback){
                    observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kNotificationPending;++observed.suspendSerial;
                    ownerSharedPowerChanged(observed);
                    ++observed.generation;observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
                }
                previousReflowShowProc=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(mid,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(observeReflowShow)));
                if(!previousReflowShowProc)throw std::runtime_error("reflow-visible-hook-unavailable");
                reflowAdmissionProbe=[] {observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kNotificationPending;};
                if(readyFallback){rejectNextReflowTimer=true;ownerSharedPowerChanged(observed);}
                else if(old.timer)ownerDisplayRefreshTimerProc(old.window,WM_TIMER,old.timer,0);
                const bool closedAtShow=!reflowAdmissionProbe&&!sharedInputAllowed();
                reflowAdmissionProbe=nullptr;SetWindowLongPtrW(mid,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(previousReflowShowProc));
                check(label+"-closes-after-real-hwnd-show",queued&&closedAtShow&&reflowTimerRejections==rejected+(readyFallback?1:0));
                check(label+"-does-not-publish-partial-show",ownerDisplayRefresh.completed==completed&&ownerDisplayReflowInterrupted&&
                    displayTransitionFence.idleSuspended&&!ownerDisplayRefresh.pending&&!visibleScreenControls()&&autoHide.hidden&&autoHide.lastTouch==idleStart);
                // The pending admission fact was coalesced; only the next Ready is delivered.
                observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;ownerSharedPowerChanged(observed);
                const auto restored=ownerDisplayRefresh;
                check(label+"-next-ready-owns-new-timer",restored.pending&&restored.timer&&restored.timer!=old.timer);
                if(restored.timer)ownerDisplayRefreshTimerProc(restored.window,WM_TIMER,restored.timer,0);
                tick();tick();tick();tick();
                check(label+"-restores-idle-input-without-repair",ownerDisplayRefresh.completed==completed+1&&!ownerDisplayReflowInterrupted&&
                    !displayTransitionFence.idleSuspended&&ownerResumeControlsHealthy()&&autoHide.hidden&&autoHide.lastTouch==idleStart&&
                    !IsWindowVisible(outlines[0])&&!IsWindowVisible(outlines[1])&&ownerFaultBudget.repairs==0&&
                    (!readyFallback||(ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&ownerResumeRecovery.powerGeneration==observed.generation)));
                if(!ownerResumeControlsHealthy())configure(getConfig(),1,true);
                pump();
            }
            const bool queuedBeforeOff=queueOwnerDisplayRefresh(true);const auto beforeOff=ownerDisplayRefresh;
            observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kNotificationPending;
            ownerDisplayRefreshTimerProc(beforeOff.window,WM_TIMER,beforeOff.timer,0);
            auto off=getConfig();off.masterEnabled=false;configure(off,1,true);
            observed.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
            ownerSharedPowerChanged(observed);const auto afterOff=ownerDisplayRefresh;
            if(afterOff.timer)ownerDisplayRefreshTimerProc(afterOff.window,WM_TIMER,afterOff.timer,0);
            check("verify-admission-reflow-cannot-revive-master-off",queuedBeforeOff&&!getConfig().masterEnabled&&!ownerDisplayRefresh.pending&&
                !IsWindowVisible(windows[0])&&!IsWindowVisible(windows[1])&&!buttonsVisible());
            const auto settled=ownerDisplayRefresh;
            const auto settledPadEpoch=padRefreshEpoch,settledButtonEpoch=buttonRefreshEpoch;
            const auto settledOutlineUploads=outlineUploadCount;
            unsigned acceptedWhileOff=0;
            for(unsigned i=0;i<64;++i){
                if(queueOwnerDisplayRefresh((i%2)==0))++acceptedWhileOff;
                const auto work=ownerDisplayRefresh;
                if(work.timer)ownerDisplayRefreshTimerProc(work.window,WM_TIMER,work.timer,0);
            }
            report["masterOffDisplay"]={{"notifications",64},{"accepted",acceptedWhileOff},
                {"completed",ownerDisplayRefresh.completed-settled.completed},{"padRefreshes",padRefreshEpoch-settledPadEpoch},
                {"buttonRefreshes",buttonRefreshEpoch-settledButtonEpoch},{"padOutlineUploads",outlineUploadCount-settledOutlineUploads}};
            check("verify-settled-master-off-skips-display-work",!acceptedWhileOff&&ownerDisplayRefresh.serial==settled.serial&&
                ownerDisplayRefresh.requests==settled.requests&&ownerDisplayRefresh.completed==settled.completed&&
                padRefreshEpoch==settledPadEpoch&&buttonRefreshEpoch==settledButtonEpoch&&outlineUploadCount==settledOutlineUploads&&
                !ownerDisplayRefresh.pending&&!displayTransitionFence.idleSuspended&&!visibleScreenControls());
            // A visible remnant is not a settled OFF state.
            ShowWindow(windows[0],SW_SHOWNOACTIVATE);
            const bool remnant=IsWindowVisible(windows[0]),queuedCleanup=queueOwnerDisplayRefresh(true);
            const auto cleanup=ownerDisplayRefresh;
            if(cleanup.timer)ownerDisplayRefreshTimerProc(cleanup.window,WM_TIMER,cleanup.timer,0);
            check("verify-master-off-stale-input-still-gets-cleaned",remnant&&queuedCleanup&&cleanup.timer&&
                !ownerDisplayRefresh.pending&&!displayTransitionFence.idleSuspended&&!visibleScreenControls());
            auto on=off;on.masterEnabled=true;configure(on,1,true);pump();
            const bool queuedAfterOn=queueOwnerDisplayRefresh(true);const auto afterOn=ownerDisplayRefresh;
            if(afterOn.timer)ownerDisplayRefreshTimerProc(afterOn.window,WM_TIMER,afterOn.timer,0);
            autoHideSetHidden(true);autoHideStopTimer();
            RECT panel{};GetClientRect(windows[0],&panel);
            ymcc::screeninput::Event touch{0,929,PT_TOUCH,ymcc::screeninput::Phase::Down,panel.right*.5f,panel.bottom*.5f};
            nativeInputSink(windows[0],touch,nullptr);
            check("verify-master-reenable-keeps-display-and-first-touch",queuedAfterOn&&afterOn.timer&&ownerResumeControlsHealthy()&&
                core.pads[0].down&&!autoHide.hidden&&IsWindowVisible(outlines[0])&&IsWindowVisible(windows[1])&&buttonsVisible());
            touch.phase=ymcc::screeninput::Phase::Up;nativeInputSink(windows[0],touch,nullptr);
            auto mixed=on;mixed.autoHideSeconds=0;
            for(const bool buttonsOnly:{true,false}){
                auto selected=mixed;selected.enabled=!buttonsOnly;
                if(!buttonsOnly){
                    selected.summonEnabled=false;selected.summonPosition=SummonPosition::Off;
                    selected.specialEnabled=false;selected.specialMask=0;selected.standaloneSpecialMode=StandaloneSpecialMode::Off;
                    selected.rearEnabled=false;selected.rearMask=0;
                }
                configure(selected,1,true);pump();
                const auto padUploads=outlineUploadCount,buttonUploadsBefore=buttonUploadCount;
                unsigned reflows=0;
                for(unsigned i=0;i<16;++i){
                    if(queueOwnerDisplayRefresh(true))++reflows;
                    const auto work=ownerDisplayRefresh;
                    if(work.timer)ownerDisplayRefreshTimerProc(work.window,WM_TIMER,work.timer,0);
                }
                const auto padDelta=outlineUploadCount-padUploads,buttonDelta=buttonUploadCount-buttonUploadsBefore;
                const std::string label=buttonsOnly?"buttons-only":"pads-only";
                report["partialDisplay"][label]={{"reflows",reflows},{"padOutlineUploads",padDelta},{"buttonOutlineUploads",buttonDelta}};
                check("verify-"+label+"-refreshes-only-selected-artwork",reflows==16&&
                    (buttonsOnly?(padDelta==0&&buttonDelta>=16):(padDelta>=32&&buttonDelta==0)));
                const bool padsVisible=IsWindowVisible(windows[0])&&IsWindowVisible(windows[1])&&
                    IsWindowVisible(outlines[0])&&IsWindowVisible(outlines[1]);
                const bool padsHidden=!IsWindowVisible(windows[0])&&!IsWindowVisible(windows[1])&&
                    !IsWindowVisible(outlines[0])&&!IsWindowVisible(outlines[1]);
                check("verify-"+label+"-keeps-active-controls-healthy",ownerResumeControlsHealthy()&&
                    (buttonsOnly?(padsHidden&&buttonsVisible()):(padsVisible&&!buttonsVisible())));
            }
            RECT priorPad{},priorButton{};GetWindowRect(windows[0],&priorPad);GetWindowRect(buttonWindows[1],&priorButton);
            mixed.scale+=10;configure(mixed,1,true);pump();
            RECT newPad{},newButton{};GetWindowRect(windows[0],&newPad);GetWindowRect(buttonWindows[1],&newButton);
            check("verify-unselected-artwork-reenable-uses-current-geometry",ownerResumeControlsHealthy()&&
                IsWindowVisible(outlines[0])&&IsWindowVisible(buttonOutlines[1])&&
                newPad.right-newPad.left!=priorPad.right-priorPad.left&&newButton.right-newButton.left!=priorButton.right-priorButton.left&&
                outlineUploads[0].width==newPad.right-newPad.left&&buttonUploads[1].width==newButton.right-newButton.left);
            rejectNextReflowTimer=true;
            const bool rejectedWithoutWithdrawal=!queueOwnerDisplayRefresh(true)&&!rejectNextReflowTimer&&
                !ownerDisplayRefresh.pending&&IsWindowVisible(windows[0])&&buttonsVisible();
            const auto idleRejected=ownerDisplayExportState();report["displayExport"]["idleRejected"]=idleRejected;
            check("verify-reflow-export-reports-timer-rejection-without-withdrawal",rejectedWithoutWithdrawal&&
                idleRejected["phase"]=="timer-rejected"&&idleRejected["phaseError"]==ERROR_NOT_ENOUGH_MEMORY);
            const auto checksBeforeExport=ownerVisibilityChecks,uploadsBeforeExport=outlineUploadCount;
            const auto exportReady=[](HWND window){
                MSG m{};const auto deadline=GetTickCount64()+400;
                while(GetTickCount64()<deadline){
                    if(PeekMessageW(&m,window,kOwnerExportMessage,kOwnerExportMessage,PM_REMOVE)){DispatchMessageW(&m);return true;}
                    Sleep(1);
                }
                return false;
            };
            const auto onOwner=GetCurrentThreadId();
            auto goodExport=std::async(std::launch::async,[]{return requestOwnerExport();});
            const bool dispatchedExport=exportReady(ownerWindow);
            const auto freshExport=goodExport.get();
            check("verify-worker-export-copies-current-state-on-the-owner",dispatchedExport&&!freshExport.reason&&freshExport.snapshot.is_object()&&
                freshExport.snapshot["ownerThread"]==onOwner&&freshExport.snapshot["onOwner"]==true&&
                freshExport.snapshot["displayStack"]["restackWork"].is_object()&&ownerSnapshotCaptures==1);
            auto timedExport=std::async(std::launch::async,[]{
                auto expired=requestOwnerExport();
                auto receipt=ownerExportMissingReceipt(expired.reason);
                return std::pair{std::move(expired),std::move(receipt)};
            });
            const auto [expiredExport,missingReceipt]=timedExport.get();
            const bool dispatchedStale=exportReady(ownerWindow);
            check("verify-timed-out-export-cannot-capture-or-substitute-stale-state",expiredExport.reason&&
                std::string_view(expiredExport.reason)=="snapshot-timeout"&&expiredExport.snapshot.is_null()&&dispatchedStale&&ownerSnapshotCaptures==1);
            report["displayExport"]["missingSnapshot"]=missingReceipt;
            const auto phase=missingReceipt.value("lastPublishedDisplayTransition",Json::object());
            check("verify-timed-out-export-retains-only-last-published-reflow-phase",
                missingReceipt["name"]=="screen-touchpads-export-state.json"&&missingReceipt["present"]==false&&
                missingReceipt["missingReason"]=="snapshot-timeout"&&!missingReceipt.contains("state")&&!missingReceipt.contains("displayStack")&&
                phase==idleRejected&&phase.value("phase",std::string{})=="timer-rejected");
            auto nextExport=std::async(std::launch::async,[]{return requestOwnerExport();});
            const bool dispatchedNext=exportReady(ownerWindow);
            const auto nextSnapshot=nextExport.get();
            check("verify-next-export-is-fresh-and-does-not-run-display-work",dispatchedNext&&!nextSnapshot.reason&&ownerSnapshotCaptures==2&&
                ownerVisibilityChecks==checksBeforeExport&&outlineUploadCount==uploadsBeforeExport);
            auto retiredExport=std::async(std::launch::async,[]{return requestOwnerExport();});
            bool sawRetiredRequest=false;
            const auto admissionDeadline=GetTickCount64()+400;
            while(GetTickCount64()<admissionDeadline){
                {std::lock_guard lock(ownerExportMutex);if(ownerExportPending){sawRetiredRequest=true;break;}}
                Sleep(1);
            }
            retireOwnerExport();
            const auto cancelledExport=retiredExport.get();
            const bool dispatchedRetired=exportReady(ownerWindow);
            auto deniedExport=std::async(std::launch::async,[]{return requestOwnerExport();});
            const auto denied=deniedExport.get();
            check("verify-retirement-wakes-export-and-blocks-late-dispatch",sawRetiredRequest&&cancelledExport.reason&&
                std::string_view(cancelledExport.reason)=="owner-retired"&&dispatchedRetired&&denied.reason&&
                std::string_view(denied.reason)=="owner-retired"&&ownerSnapshotCaptures==2);
            inputPowerAllowed=nullptr;
            ownerOnSuspend();queueOwnerResumeRebind();pump();ownerResumeReady(++observed.generation);
            const auto resetDeadline=ownerResumeRecovery.deadline;
            for(unsigned stage=1;stage<=kOwnerResumeMaxRepairs;++stage){windowError=ERROR_NOT_READY;tick();}
            check("verify-uninterrupted-third-repair-really-installs-owned-channel",
                ownerResumeRecovery.deadline==resetDeadline&&ownerResumeRecovery.repairs==kOwnerResumeMaxRepairs&&
                ownerResumeRecovery.channelResets==1&&ownerFaultBudget.channelResets==1&&nativePadInputRuntime.isolatedInput&&
                nativeInputSurfaces[0].usesIndependentInput()&&nativeInputSurfaces[1].usesIndependentInput());
            tick();tick();
            check("verify-uninterrupted-channel-reset-is-verified-twice",
                ownerResumeControlsHealthy()&&ownerResumeRecovery.clean==2&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer);
            cancelOwnerResumeRecovery("fixture-end");ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;report["selector"]="--switch-storm-verification";
            {
                namespace custom=ymcc::customtouch;
                auto exitCfg=cfg;exitCfg.enabled=true;exitCfg.layout=Layout::Dual;exitCfg.left=exitCfg.right=Mode::Mouse;
                exitCfg.standaloneSpecialMode=StandaloneSpecialMode::SteamDeck;exitCfg.specialEnabled=true;exitCfg.specialMask=3;exitCfg.autoHideSeconds=0;
                for(unsigned variant:{0u,1u,2u,3u}){
                    const bool editor=variant!=0;
                    const std::string label=variant==3?"verify-exit-editor-permanent-destroy-failure":variant==2?"verify-exit-editor-transient-destroy-failure":
                        editor?"verify-exit-with-editor":"verify-exit-with-custom-key";
                    if(editor&&!initializeForOwner(GetModuleHandleW(nullptr),ownerWindow,exitCfg,0,L"owned.exe"))throw std::runtime_error("fixture exit editor owner restart");
                    setContext("disabled",false);configure(exitCfg,0,true);pump();
                    custom::dryRun=true;
                    custom::resolveKey=[](const std::string&,const std::string&,WORD& vk,bool& shift){vk=VK_INSERT;shift=false;return true;};
                    custom::initialize(custom::defaults());pump();
                    if(editor&&!custom::begin())throw std::runtime_error("fixture exit editor not open");
                    if(custom::items.size()!=1||!custom::items[0]->surface.active()||!IsWindowVisible(custom::items[0]->outline)||
                       !IsWindowVisible(windows[0])||!IsWindowVisible(windows[1])||!IsWindowVisible(buttonWindows[1])||!IsWindowVisible(buttonWindows[2])||
                       (editor&&(!custom::toolbar||!IsWindowVisible(custom::toolbar))))throw std::runtime_error("fixture exit sources unavailable");
                    const auto item=custom::items[0];
                    if(!editor){custom::input(*item,WM_POINTERDOWN,81,1,1);if(!custom::heldKeys())throw std::runtime_error("fixture key did not press");}
                    else item->contact=81; // Pending local feedback at the editor's terminal boundary; no injected touch.
                    exitReleaseObserved=exitReleaseWithdrawn=false;
                    revealFaultWindow=item->outline;revealSuccessAction=observeCustomExitRelease;
                    if(variant>=2){editorDestroyFaultWindow=custom::toolbar;editorDestroyFailuresRemaining=variant==2?3:-1;editorDestroyAttempts=0;}
                    quiesceForOwner();
                    revealSuccessAction=nullptr;revealFaultWindow=nullptr;
                    check(label+"-withdraws-all-input-before-custom-cleanup",exitReleaseObserved&&exitReleaseWithdrawn);
                    check(label+"-quiesce-releases-key-and-contact",!custom::heldKeys()&&!item->contact&&!custom::editing.load());
                    shutdownForOwner();
                    const auto receipt=exitInputReleaseState();
                    if(variant==3){
                        check(label+"-reports-live-window-with-bounded-retries",!receipt.value("verified",true)&&
                            exitInputCheckCount==exitInputChecks.size()&&exitInputChecks.back().liveWindows!=0&&
                            editorDestroyAttempts==exitInputChecks.size()+2&&IsWindow(editorDestroyFaultWindow)&&
                            !IsWindowEnabled(editorDestroyFaultWindow)&&!IsWindowVisible(editorDestroyFaultWindow));
                    }else if(variant==2){
                        check(label+"-retries-then-observes-three-clean-rounds",receipt.value("verified",false)&&exitInputCheckCount==4&&
                            !exitInputChecks[0].ok&&exitInputChecks[1].ok&&exitInputChecks[2].ok&&exitInputChecks[3].ok&&
                            editorDestroyAttempts==4&&!IsWindow(editorDestroyFaultWindow)&&!custom::toolbar);
                    }else check(label+"-three-clean-confirmations",receipt.value("verified",false)&&exitInputCheckCount==3);
                    if(variant>=2){
                        report[variant==2?"editorTransientExit":"editorPersistentExit"]={{"receipt",receipt},{"destroyAttempts",editorDestroyAttempts}};
                        if(IsWindow(editorDestroyFaultWindow))DestroyWindow(editorDestroyFaultWindow); // Fixture-owned cleanup after measuring the result.
                        custom::toolbar=nullptr;editorDestroyFaultWindow=nullptr;editorDestroyFailuresRemaining=0;
                    }
                    custom::resolveKey=nullptr;custom::dryRun=false;
                }
            }
        }else if(stormOnly){
            cfg.enabled=true;cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;cfg.autoHideSeconds=0;cfg.touchFeedback=TouchFeedbackMode::Off;
            setContext("steamdeck",true);configure(cfg,1,true);pump();
            // A selected/visible HWND with no valid submitted frame is NOT healthy.
            if(!transitionsOnly){
            outlineUploads[0].ready=false;windowError=0;
            check("storm-visible-HWND-without-artwork-is-not-healthy",!ownerResumeControlsHealthy());
#ifdef YMCC_STORM_OLD_CONTROL
            if(!passed)throw std::runtime_error("old-control-necessary-false-healthy");
#endif
            check("storm-manual-unchanged-selection-repairs-pad-frame",configure(cfg,1,true)&&ownerResumeControlsHealthy());
            buttonUploads[1].ready=false;
            check("storm-button-frame-loss-is-not-healthy",!ownerResumeControlsHealthy());
            check("storm-manual-selection-repairs-button-frame",configure(cfg,1,true)&&ownerResumeControlsHealthy());
            // Existing hooks must not swallow the re-show request in a stable game.
            stopOwnerVisibilityRechecks();ownerVisibilityQueued=false;ownerVisibilityArtworkRequested=false;
            syncOwnerVisibility();
            check("storm-existing-subscription-queues-immediate-artwork",ownerVisibilityQueued&&ownerVisibilityArtworkRequested);
            pump();
            }
            const auto settings=configJson(getConfig());
            for(int round=0;round<64&&passed;++round){
                const auto label="storm-"+std::to_string(round);const HWND target=targets[round%2].window;
                check(label+"-real-peer-resize-mode-and-promote",message(target,shapeMessage,(WPARAM)round)&&message(target,promoteMessage));
                RECT targetRect{};GetWindowRect(target,&targetRect);report["targetRects"].push_back({{"round",round},{"target",(ULONG_PTR)target},{"rect",{targetRect.left,targetRect.top,targetRect.right,targetRect.bottom}},{"iconic",bool(IsIconic(target))}});
                const auto source=nativeInputSurfaces[0].child();const auto builds=outlineBitmapBuilds,frames=pressFrameBuilds;
                RECT rc{};GetClientRect(windows[0],&rc);
                ymcc::screeninput::Event down{0,(UINT32)(2000+round),PT_TOUCH,ymcc::screeninput::Phase::Down,rc.right*.5f,rc.bottom*.5f};nativeInputSink(windows[0],down,nullptr);
                for(int burst=0;burst<16;++burst){for(int side=0;side<2;++side)restoreScreenLayerPair(windows[side],outlines[side],target,true);for(int i=0;i<7;++i)restoreScreenLayerPair(buttonWindows[i],buttonOutlines[i],target,true);}
                const HWND before=GetForegroundWindow();restoreOwnerArtwork(ownerVisibilityEpoch,target);
                check(label+"-contact-source-and-focus-preserved",core.pads[0].down&&source==nativeInputSurfaces[0].child()&&before==GetForegroundWindow());
                check(label+"-cached-artwork-and-input-health",outlineBitmapBuilds==builds&&pressFrameBuilds==frames&&ownerResumeControlsHealthy());
                check(label+"-all-layers-above-real-peer",!screenLayerOccluded(windows[0],target)&&!screenLayerOccluded(outlines[0],target)&&!screenLayerOccluded(buttonOutlines[1],target));
                check(label+"-mouse-cross-process-hit",below(target,round,report["hitRows"]));
                auto up=down;up.phase=ymcc::screeninput::Phase::Up;nativeInputSink(windows[0],up,nullptr);
                check(label+"-signals-released",!core.pads[0].down&&!sentKeys&&!sentClick&&!screenButtonMask.load());
                if(round%8==7){
                    cfg.enabled=false;configure(cfg,1,true);pump();cfg.enabled=true;configure(cfg,1,true);
                    check(label+"-reenable-needs-no-extra-game-toggle",ownerVisibilityQueued&&ownerVisibilityArtworkRequested&&ownerResumeControlsHealthy());pump();
                    ShowWindow(outlines[0],SW_HIDE);
                    check(label+"-missing-pad-detected-and-manually-recovered",!ownerResumeControlsHealthy()&&configure(cfg,1,true)&&ownerResumeControlsHealthy());
                    autoHideSetHidden(true);const auto uploads=outlineUploadCount;restoreOwnerArtwork(ownerVisibilityEpoch,target);
                    check(label+"-intentional-autohide-not-revived",autoHide.hidden&&!IsWindowVisible(outlines[0])&&outlineUploadCount==uploads&&ownerResumeControlsHealthy());autoHideSetHidden(false);
                }
                check(label+"-settings-unchanged",settings==configJson(getConfig()));
            }
            if(!passed)throw std::runtime_error("storm-transition-failure-no-later-business");
            if(!transitionsOnly){
            // Actual independent-channel work is serviced by the bounded health probe.
            ymcc::sleeppower::SharedPowerV1 power;power.generation=510;power.flags=ymcc::sleeppower::kKnown|ymcc::sleeppower::kHardwareAllowed;
            static ymcc::sleeppower::SharedPowerV1 observed;observed=power;
            ownerPowerCapture=[](ymcc::sleeppower::SharedPowerV1& out){out=observed;return true;};
            ownerPowerCurrent=[](const ymcc::sleeppower::SharedPowerV1& p){return p.generation==observed.generation&&p.suspendSerial==observed.suspendSerial&&ymcc::sleeppower::powerAdmitted(observed);};
            ownerOnSuspend();queueOwnerResumeRebind();pump();ownerResumeReady(power.generation);
            bool dispatched=false;auto action=nativeInputSurfaces[0].source().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{dispatched=true;});
            const auto beforeProbe=outlineUploadCount;const auto wake=ownerResumeRecovery;ownerResumeTimerProc(wake.window,WM_TIMER,wake.timer,0);
            check("storm-bounded-health-services-real-input-channel-and-submits-frame",dispatched&&outlineUploadCount>beforeProbe&&ownerResumeRecovery.clean==1);
            const auto second=ownerResumeRecovery;ownerResumeTimerProc(second.window,WM_TIMER,second.timer,0);
            check("storm-two-current-checks-confirm-health-without-repair",!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&ownerResumeRecovery.clean==2&&ownerResumeRecovery.repairs==0);
            ownerOnSuspend();queueOwnerResumeRebind();pump();++observed.generation;ownerResumeReady(observed.generation);
            bool exitRan=false;auto exiting=nativeInputSurfaces[0].source().Dispatcher().RunAsync(winrt::Windows::UI::Core::CoreDispatcherPriority::Normal,[&]{exitRan=true;quiesceForOwner();});
            const auto last=ownerResumeRecovery;ownerResumeTimerProc(last.window,WM_TIMER,last.timer,0);
            check("storm-probe-reentry-cannot-publish-healthy-or-revive-after-exit",exitRan&&inputTeardown&&!ownerResumeRecovery.pending&&!ownerResumeRecovery.timer&&!buttonsVisible());
            ownerPowerCapture=nullptr;ownerPowerCurrent=nullptr;
            }
            report["selector"]=transitionsOnly?"--switch-storm-transitions":"--switch-storm";report["rounds"]=64;report["promotionBurstsPerRound"]=16;
        }else if(!artworkOnly&&!preservationOnly){
        for(int round=0;round<64;++round){
            const auto label="round-"+std::to_string(round);cfg.enabled=true;cfg.scale=80+(round%3)*20;cfg.transparency=(round%3)*50;
            // Eight rounds per layout/persona let every drift kind cover each.
            switch((round/8)%4){
            case 0:cfg.layout=Layout::Single;cfg.single=Mode::Mouse;setContext("disabled",false);break;
            case 1:cfg.layout=Layout::Dual;cfg.left=Mode::Wasd;cfg.right=Mode::Mouse;setContext("disabled",false);break;
            case 2:cfg.layout=Layout::Dual;cfg.left=cfg.right=Mode::Deck;setContext("steamdeck",true);break;
            case 3:cfg.layout=Layout::Single;cfg.single=Mode::DualSense;setContext("dualsense-edge",true);break;
            }
            configure(cfg);refresh();pump();
            check(label+"-controlled-source-ready",nativeInputSurfaces[0].active());
            const HWND child=nativeInputSurfaces[0].child();mutate(round%8);
            HWND target=targets[round%2].window;check(label+"-owned-game-switch",message(target,promoteMessage));
            for(int side=0;side<2;++side)restoreScreenLayerPair(windows[side],outlines[side],target,round%2==0);
            for(int i=0;i<7;++i)restoreScreenLayerPair(buttonWindows[i],buttonOutlines[i],target,round%2==0);
            check(label+"-mouse-targets-lower-process",below(target,round,report["hitRows"]));
            check(label+"-source-stays-enabled-and-is-not-recreated",nativeInputSurfaces[0].active()&&nativeInputSurfaces[0].child()==child&&IsWindowEnabled(windows[0])&&IsWindowEnabled(child));
            const bool displaySafe=!IsWindowEnabled(outlines[0])&&
                (GetWindowLongPtrW(outlines[0],GWL_EXSTYLE)&(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE))==(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE);
            check(label+"-visual-stays-inert",displaySafe);
            for(int i=0;i<7;++i)if(buttonWindows[i]&&IsWindowVisible(buttonWindows[i])){
                check(label+"-button-"+std::to_string(i)+"-source",nativeButtonSurfaces[i].active());
                EnableWindow(buttonOutlines[i],TRUE);
                if(round%4==0)SetWindowLongPtrW(buttonOutlines[i],GWL_EXSTYLE,GetWindowLongPtrW(buttonOutlines[i],GWL_EXSTYLE)&~WS_EX_LAYERED);
                check(label+"-button-"+std::to_string(i)+"-per-pixel-bitmap-retained",buttonUploads[i].ready);
                check(label+"-button-"+std::to_string(i)+"-visual-inert",!IsWindowEnabled(buttonOutlines[i]));
            }
            // Mapping sink simulation, not physical Touch/Pen acceptance.
            RECT rect{};GetClientRect(windows[0],&rect);const float x=rect.right*.5f,y=rect.bottom*.5f;
            ymcc::screeninput::Event down{0,(UINT32)(100+round),PT_TOUCH,ymcc::screeninput::Phase::Down,x,y};
            nativeInputSink(windows[0],down,nullptr);check(label+"-touch-mapping-still-admitted",core.pads[0].down);
            HWND next=targets[(round+1)%2].window;check(label+"-second-switch",message(next,promoteMessage));
            const auto uploads=nativeInputSurfaces[0].uploads;
            for(int side=0;side<2;++side)restoreScreenLayerPair(windows[side],outlines[side],next,round%2==0);
            for(int i=0;i<7;++i)restoreScreenLayerPair(buttonWindows[i],buttonOutlines[i],next,round%2==0);
            check(label+"-ordinary-switch-preserves-contact-and-bitmap-cache",core.pads[0].down&&nativeInputSurfaces[0].uploads==uploads&&nativeInputSurfaces[0].child()==child);
            auto up=down;up.phase=ymcc::screeninput::Phase::Up;nativeInputSink(windows[0],up,nullptr);check(label+"-touch-mapping-releases",!core.pads[0].down);
            check(label+"-second-switch-mouse-pass",below(next,round,report["hitRows"]));
            if(round%16==15){cfg.enabled=false;configure(cfg);pump();check(label+"-hidden-input-host",!IsWindowVisible(windows[0]));cfg.enabled=true;configure(cfg);pump();check(label+"-reshow-is-native-ready",IsWindowVisible(windows[0])&&nativeInputSurfaces[0].active());}
        }
        ownerOnSuspend();check("resume-rebind-queued",queueOwnerResumeRebind());pump();check("resume-rebind-source-ready",nativeInputSurfaces[0].active());
        // Reproduce the prior opaque fallback: sync fails and close restores the
        // old non-layered style. A failed refresh must NOT show that HWND.
        DestroyWindow(nativeInputSurfaces[0].child());refresh();
        check("failed-native-refresh-hides-pad-input-and-outline",!IsWindowVisible(windows[0])&&!IsWindowVisible(outlines[0]));
        refresh();check("next-refresh-can-recover-controlled-source",IsWindowVisible(windows[0])&&nativeInputSurfaces[0].active());
        DestroyWindow(nativeInputSurfaces[0].child());restoreScreenLayerPair(windows[0],outlines[0],targets[0].window);
        check("failed-raise-validation-hides-pad-input-and-outline",!IsWindowVisible(windows[0])&&!IsWindowVisible(outlines[0]));

        }else if(preservationOnly){
            stopOwnerVisibilityRechecks();const HWND target=targets[0].window;
            if(!message(target,promoteMessage))throw std::runtime_error("owned target promotion failed");
            restoreScreenLayerPair(windows[0],outlines[0],target,true);
            for(int i=0;i<7;++i)restoreScreenLayerPair(buttonWindows[i],buttonOutlines[i],target,true);
            RECT inputBefore{};if(!GetWindowRect(windows[0],&inputBefore))throw std::runtime_error("input rectangle unavailable");
            const auto settings=configJson(getConfig());RECT rc{};GetClientRect(windows[0],&rc);
            ymcc::screeninput::Event down{0,917,PT_TOUCH,ymcc::screeninput::Phase::Down,rc.right*.5f,rc.bottom*.5f};nativeInputSink(windows[0],down,nullptr);
            const HWND foregroundBefore=GetForegroundWindow();restoreOwnerArtwork(ownerVisibilityEpoch,target);
            RECT inputAfter{};const bool rectAfter=GetWindowRect(windows[0],&inputAfter)!=FALSE;const HWND foregroundAfter=GetForegroundWindow();
            check("artwork-input-geometry-preserved",rectAfter&&EqualRect(&inputBefore,&inputAfter));
            check("artwork-configuration-preserved",settings==configJson(getConfig()));
            check("artwork-foreground-preserved",foregroundBefore==foregroundAfter);
            report["preservation"]={{"rectBefore",{inputBefore.left,inputBefore.top,inputBefore.right,inputBefore.bottom}},{"rectAfter",{inputAfter.left,inputAfter.top,inputAfter.right,inputAfter.bottom}},{"foregroundBefore",(ULONG_PTR)foregroundBefore},{"foregroundAfter",(ULONG_PTR)foregroundAfter},{"target",(ULONG_PTR)target},{"legacyTargetEquality",foregroundAfter==target}};
            auto up=down;up.phase=ymcc::screeninput::Phase::Up;nativeInputSink(windows[0],up,nullptr);
            report["selector"]="--artwork-preservation";
        }else{
            stopOwnerVisibilityRechecks();
            const HWND target=targets[0].window;check("artwork-owned-target-promotion",message(target,promoteMessage));
            restoreScreenLayerPair(windows[0],outlines[0],target,true);for(int i=0;i<7;++i)restoreScreenLayerPair(buttonWindows[i],buttonOutlines[i],target,true);
            const auto child=nativeInputSurfaces[0].child();const auto settings=configJson(getConfig());RECT inputBefore{};GetWindowRect(windows[0],&inputBefore);
            const auto bitmaps=outlineBitmapBuilds,frames=pressFrameBuilds,uploads=outlineUploadCount,buttons=buttonUploadCount;
            const auto padFrame=outlineFrames[0][0].dc;unsigned visibleButtons=0;for(HWND h:buttonOutlines)if(h&&IsWindowVisible(h))++visibleButtons;
            RECT rc{};GetClientRect(windows[0],&rc);ymcc::screeninput::Event down{0,917,PT_TOUCH,ymcc::screeninput::Phase::Down,rc.right*.5f,rc.bottom*.5f};nativeInputSink(windows[0],down,nullptr);
            check("artwork-held-touch-admitted",core.pads[0].down);const auto pressedUploads=outlineUploadCount,pressedButtons=buttonUploadCount;const auto pressedFrames=pressFrameBuilds;
            const HWND foregroundBefore=GetForegroundWindow();
            restoreOwnerArtwork(ownerVisibilityEpoch,target);
            check("artwork-real-layered-bitmap-submissions",outlineUploadCount==pressedUploads+1&&buttonUploadCount==pressedButtons+visibleButtons);
            check("artwork-cached-frames-and-bitmaps-reused",outlineBitmapBuilds==bitmaps&&pressFrameBuilds==pressedFrames&&outlineFrames[0][0].dc==padFrame);
            check("artwork-held-contact-and-native-source-preserved",core.pads[0].down&&nativeInputSurfaces[0].child()==child);
            RECT inputAfter{};const bool rectAfter=GetWindowRect(windows[0],&inputAfter)!=FALSE;const HWND foregroundAfter=GetForegroundWindow();
            check("artwork-input-geometry-preserved",rectAfter&&EqualRect(&inputBefore,&inputAfter));
            check("artwork-configuration-preserved",settings==configJson(getConfig()));
            check("artwork-foreground-preserved",foregroundBefore==foregroundAfter);
            report["preservation"]={{"rectBefore",{inputBefore.left,inputBefore.top,inputBefore.right,inputBefore.bottom}},{"rectAfter",{inputAfter.left,inputAfter.top,inputAfter.right,inputAfter.bottom}},{"foregroundBefore",(ULONG_PTR)foregroundBefore},{"foregroundAfter",(ULONG_PTR)foregroundAfter},{"target",(ULONG_PTR)target},{"legacyTargetEquality",foregroundAfter==target}};
            check("artwork-native-mouse-hit-still-targets-lower-process",below(target,701,report["hitRows"]));
            const HWND dedicated=buttonOutlines[1],buttonChild=nativeButtonSurfaces[1].child();
            ShowWindow(outlines[0],SW_HIDE);ShowWindow(dedicated,SW_HIDE);
            const auto beforeLost=outlineUploadCount;
            restoreOwnerArtwork(ownerVisibilityEpoch,target);
            check("artwork-missing-wanted-pad-reshown-with-cached-frame",!autoHide.hidden&&IsWindowVisible(outlines[0])&&
                outlineUploadCount==beforeLost+1&&outlineBitmapBuilds==bitmaps&&pressFrameBuilds==pressedFrames);
            check("artwork-missing-wanted-button-reshown-with-same-source",buttonChild&&IsWindowVisible(dedicated)&&
                nativeButtonSurfaces[1].child()==buttonChild);
            check("artwork-reshow-preserves-held-touch-focus-and-mouse-pass",core.pads[0].down&&
                nativeInputSurfaces[0].child()==child&&foregroundBefore==GetForegroundWindow()&&below(target,702,report["hitRows"]));

            ShowWindow(outlines[0],SW_HIDE);ShowWindow(dedicated,SW_HIDE);
            revealFaultWindow=outlines[0];revealFaultsRemaining=1;revealSubmissions=0;
            restoreOwnerArtwork(ownerVisibilityEpoch,target);
            check("artwork-failed-hidden-pad-submit-does-not-show-it-or-block-button",revealSubmissions==1&&
                !revealFaultsRemaining&&!IsWindowVisible(outlines[0])&&IsWindowVisible(dedicated)&&core.pads[0].down);
            revealFaultsRemaining=0;windowError=0;
            revealSuccessAction=[](){autoHideSetHidden(true);};
            restoreOwnerArtwork(ownerVisibilityEpoch,target);
            check("artwork-successful-submit-cannot-reshow-after-reentrant-auto-hide",!revealSuccessAction&&
                autoHide.hidden&&!IsWindowVisible(outlines[0])&&!IsWindowVisible(dedicated));
            revealSuccessAction=nullptr;revealFaultWindow=nullptr;
            autoHideSetHidden(true);const auto beforeHidden=outlineUploadCount;restoreOwnerArtwork(ownerVisibilityEpoch,target);
            check("artwork-auto-hidden-pad-not-resent-or-reshown",autoHide.hidden&&outlineUploadCount==beforeHidden&&!IsWindowVisible(outlines[0]));
            autoHideSetHidden(false);
            const auto beforeStale=outlineUploadCount;const auto oldEpoch=ownerVisibilityEpoch;cancelOwnerVisibilityWork();restoreOwnerArtwork(oldEpoch,target);
            check("artwork-stale-epoch-makes-no-native-submission",outlineUploadCount==beforeStale);
            auto up=down;up.phase=ymcc::screeninput::Phase::Up;nativeInputSink(windows[0],up,nullptr);check("artwork-held-touch-releases-normally",!core.pads[0].down);
            const auto submitted=outlineUploadCount;inputTeardown=true;restoreOwnerArtwork(ownerVisibilityEpoch,target);check("artwork-terminal-admission-refuses-native-submission",outlineUploadCount==submitted);inputTeardown=false;
            report["artworkVisibleButtons"]=visibleButtons;report["artworkRenderError"]=windowError;report["artworkInitialUploads"]=uploads+buttons;report["artworkInitialFrames"]=frames;
        }
        report["ownerState"]=ownerStartupState();
    }catch(const winrt::hresult_error& e){passed=false;report["failure"]=(long)e.code();}
    catch(const std::exception& e){passed=false;report["failure"]=e.what();}
    shutdownForOwner();if(ownerWindow)DestroyWindow(ownerWindow);
    if(report.contains("displayExport")){
        const auto inactive=ownerDisplayExportState();report["displayExport"]["afterShutdown"]=inactive;
        check("verify-reflow-export-inactive-after-shutdown",inactive["phase"]=="inactive"&&inactive["phaseError"]==0);
    }
    for(auto& target:targets)terminal=stop(target)&&terminal;
    if(stormOnly){
        try {
            nativePadInputRuntime.compositor=nullptr;nativeInputRuntime.compositor=nullptr;
            bool queueClosed=true;
            if(nativePadInputRuntime.queue){auto close=nativePadInputRuntime.queue.ShutdownQueueAsync();const auto deadline=GetTickCount64()+4000;
                while(close.Status()==winrt::Windows::Foundation::AsyncStatus::Started&&GetTickCount64()<deadline){pump();MsgWaitForMultipleObjectsEx(0,nullptr,5,QS_ALLINPUT,MWMO_INPUTAVAILABLE);}
                queueClosed=close.Status()==winrt::Windows::Foundation::AsyncStatus::Completed;if(queueClosed)close.GetResults();nativePadInputRuntime.queue=nullptr;
            }
            check("storm-owned-runtime-and-dispatcher-queue-released",queueClosed&&!nativePadInputRuntime.compositor&&!nativePadInputRuntime.queue&&!nativeInputRuntime.compositor&&!nativeInputRuntime.queue);
        }catch(...){check("storm-owned-runtime-and-dispatcher-queue-released",false);}
    }
    if(apartment)winrt::uninit_apartment();SetThreadDesktop(original);CloseDesktop(desktop);
    if(stormOnly)check("terminal-owned-input-release-confirmed",exitInputReleaseState().value("verified",false));
    check("owned-lower-processes-terminal",terminal);report["ownedTargetsTerminal"]=terminal;report["cases"]=cases;report["ok"]=passed;
    std::ofstream file(output,std::ios::binary);file<<report.dump(2);
    std::cout<<"HIDDEN_GAME_SWITCH_MOUSE_PASS ok="<<passed<<" checks="<<cases.size()<<" hitRows="<<report["hitRows"].size()<<"\n";
    return passed&&file.good()?0:2;
}
