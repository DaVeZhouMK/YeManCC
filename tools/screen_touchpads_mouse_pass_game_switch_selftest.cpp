// Production touchpad/visibility code on a never-displayed desktop. Two owned
// lower processes emulate game switches. Native hit testing, not click replay.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <screen_touchpad_owner.h>
#include <iostream>
using namespace ymcc::screenpads;
static constexpr UINT promoteMessage=WM_APP+701;
static Json cases=Json::array();static bool passed=true;
static void check(const std::string& name,bool ok){passed=passed&&ok;cases.push_back({{"name",name},{"ok",ok}});}
static void pump(){MSG m{};while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){TranslateMessage(&m);DispatchMessageW(&m);}}
static LRESULT CALLBACK targetProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==promoteMessage){SetWindowPos(h,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);SetForegroundWindow(h);return 1;}
    if(m==WM_CLOSE){DestroyWindow(h);return 0;}if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK ownerProc(HWND h,UINT m,WPARAM w,LPARAM l){
    if(m==kOwnerVisibilityMessage){consumeOwnerVisibility((UINT_PTR)w);return 0;}
    if(m==kOwnerRebindMessage){consumeOwnerRebind((UINT_PTR)w);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static HWND create(const wchar_t* name,WNDPROC proc,DWORD ex,DWORD style,HWND parent=nullptr){
    WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=name;c.lpfnWndProc=proc;
    if(!RegisterClassW(&c)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return nullptr;
    return CreateWindowExW(ex,name,L"Owned hidden desktop fixture",style,-8000,-8000,16000,16000,parent,nullptr,c.hInstance,nullptr);
}
struct Target{PROCESS_INFORMATION pi{};HWND window=nullptr;};
static bool message(HWND window,UINT m){DWORD_PTR r=0;return SendMessageTimeoutW(window,m,0,0,SMTO_BLOCK|SMTO_ABORTIFHUNG,3000,&r)!=0;}
static bool start(Target& t,const std::wstring& directory,const std::wstring& desktop,int index){
    wchar_t self[32768]{};if(!GetModuleFileNameW(nullptr,self,32768))return false;
    const std::wstring ready=directory+L"\\target-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(index)+L".txt";
    std::wstring command=L"\""+std::wstring(self)+L"\" --target \""+ready+L"\"",desk=desktop;
    STARTUPINFOW si{sizeof(si)};si.lpDesktop=desk.data();si.dwFlags=STARTF_USESHOWWINDOW;si.wShowWindow=SW_HIDE;
    if(!CreateProcessW(self,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&t.pi))return false;
    CloseHandle(t.pi.hThread);t.pi.hThread=nullptr;
    for(int retry=0;retry<100&&!t.window;retry++){
        std::ifstream f{std::filesystem::path(ready)};unsigned long long hwnd=0;if(f>>hwnd)t.window=(HWND)(ULONG_PTR)hwnd;
        if(!t.window)Sleep(20);
    }
    DWORD pid=0;return t.window&&IsWindow(t.window)&&GetWindowThreadProcessId(t.window,&pid)&&pid==t.pi.dwProcessId&&pid!=GetCurrentProcessId();
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
    if(argc!=2)return 2;
    const auto output=std::filesystem::path(argv[1]),directory=output.parent_path();
    Json report={{"ok",false},{"rounds",64},{"hiddenDesktop",true},{"visibleDesktopSwitched",false},{"systemInputInjected",false},
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
            for(int side=0;side<2;++side)restoreScreenLayerPair(windows[side],outlines[side],target);
            for(int i=0;i<7;++i)restoreScreenLayerPair(buttonWindows[i],buttonOutlines[i],target);
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
            for(int side=0;side<2;++side)restoreScreenLayerPair(windows[side],outlines[side],next);
            for(int i=0;i<7;++i)restoreScreenLayerPair(buttonWindows[i],buttonOutlines[i],next);
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
        report["ownerState"]=ownerStartupState();
    }catch(const winrt::hresult_error& e){passed=false;report["failure"]=(long)e.code();}
    catch(const std::exception& e){passed=false;report["failure"]=e.what();}
    shutdownForOwner();if(ownerWindow)DestroyWindow(ownerWindow);
    for(auto& target:targets)terminal=stop(target)&&terminal;
    if(apartment)winrt::uninit_apartment();SetThreadDesktop(original);CloseDesktop(desktop);
    check("owned-lower-processes-terminal",terminal);report["ownedTargetsTerminal"]=terminal;report["cases"]=cases;report["ok"]=passed;
    std::ofstream file(output,std::ios::binary);file<<report.dump(2);
    std::cout<<"HIDDEN_GAME_SWITCH_MOUSE_PASS ok="<<passed<<" checks="<<cases.size()<<" hitRows="<<report["hitRows"].size()<<"\n";
    return passed&&file.good()?0:2;
}
