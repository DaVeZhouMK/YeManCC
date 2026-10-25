// Native hit testing on an isolated, never-displayed desktop with a different
// target process. Does not move the user's mouse, focus games, or synthesize input.
#include "../native/monitor_hud_input_policy.h"
#include <cstdio>
#include <cstdlib>
#include <string>
static int checks = 0, visibleStyleLosses = 0;
static void check(bool ok, const char* label) {
    ++checks; if (!ok) { std::fprintf(stderr,"FAIL: %s (error=%lu)\n",label,GetLastError()); std::exit(1); }
}
static LRESULT CALLBACK targetProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    static int clicks = 0;
    if(msg==WM_LBUTTONDOWN) return ++clicks;
    if(msg==WM_APP+1) return clicks;
    if(msg==WM_APP+2) { SetForegroundWindow(hwnd); return 1; }
    if(msg==WM_CLOSE) { DestroyWindow(hwnd); return 0; }
    if(msg==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hwnd,msg,w,l);
}
static LRESULT CALLBACK hudProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    if(msg==WM_STYLECHANGING&&ymcc::hudinput::guardStyle(w,l))return 0;
    if(msg==WM_STYLECHANGED && IsWindowVisible(hwnd) &&
       (GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&ymcc::hudinput::requiredExStyle)!=ymcc::hudinput::requiredExStyle)++visibleStyleLosses;
    if(msg==WM_STYLECHANGED||msg==WM_ENABLE) { if(!ymcc::hudinput::ensure(hwnd)) ShowWindow(hwnd,SW_HIDE); return 0; }
    if(msg==WM_NCHITTEST) return HTTRANSPARENT;
    if(msg==WM_TOUCHHITTESTING) return ymcc::hudinput::rejectTouchHitTest(l);
    if(msg==WM_POINTERACTIVATE) return PA_NOACTIVATE;
    if(msg==WM_MOUSEACTIVATE) return MA_NOACTIVATE;
    if(msg==WM_SETFOCUS||msg==WM_ACTIVATE) return 0;
    return DefWindowProcW(hwnd,msg,w,l);
}
static HWND create(const wchar_t* name, WNDPROC proc, DWORD ex, DWORD style, int x,int y,int w,int h) {
    WNDCLASSEXW cls{sizeof(cls)};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=name;
    RegisterClassExW(&cls);return CreateWindowExW(ex,name,name,style,x,y,w,h,nullptr,nullptr,cls.hInstance,nullptr);
}
static ULONG_PTR message(HWND hwnd,UINT msg,WPARAM w=0,LPARAM l=0) {
    DWORD_PTR result=0;
    check(SendMessageTimeoutW(hwnd,msg,w,l,SMTO_ABORTIFHUNG|SMTO_BLOCK,2000,&result)!=0,"bounded native cross-process message");
    return result;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--target") {
        HWND target=create(L"YMCC_HudInputTestTarget",targetProc,0,WS_POPUP,40,40,320,220);
        if(!target)return 2;ShowWindow(target,SW_SHOWNOACTIVATE);
        FILE* f=nullptr;if(_wfopen_s(&f,argv[2],L"w"))return 3;
        std::fprintf(f,"%llu",static_cast<unsigned long long>(reinterpret_cast<ULONG_PTR>(target)));std::fclose(f);
        MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}return 0;
    }
    check(argc==2,"isolated test output directory required");
    const auto original=GetThreadDesktop(GetCurrentThreadId());
    const auto name=L"YMCC_HudInputTest_"+std::to_wstring(GetCurrentProcessId());
    HDESK desktop=CreateDesktopW(name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
    check(desktop!=nullptr,"create hidden test desktop");
    check(SetThreadDesktop(desktop)!=FALSE,"attach only the test process thread; never switch visible desktop");
    const std::wstring ready=std::wstring(argv[1])+L"\\target-hwnd-"+std::to_wstring(GetCurrentProcessId())+L".txt";
    wchar_t executable[32768]{},station[1024]{};DWORD bytes=0;
    check(GetModuleFileNameW(nullptr,executable,32768)>0,"test executable path");
    check(GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,station,sizeof(station),&bytes)!=FALSE,"current window station identity");
    std::wstring command=L"\""+std::wstring(executable)+L"\" --target \""+ready+L"\"",desktopPath=std::wstring(station)+L"\\"+name;
    STARTUPINFOW startup{sizeof(startup)};startup.lpDesktop=desktopPath.data();PROCESS_INFORMATION pi{};
    check(CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&pi)!=FALSE,"target process on same hidden desktop");
    CloseHandle(pi.hThread);HWND target=nullptr;
    for(int attempt=0;attempt<100&&!target;++attempt){FILE* f=nullptr;if(!_wfopen_s(&f,ready.c_str(),L"r")){unsigned long long value=0;if(std::fscanf(f,"%llu",&value)==1)target=reinterpret_cast<HWND>(static_cast<ULONG_PTR>(value));std::fclose(f);}if(!target)Sleep(50);}
    check(target&&IsWindow(target),"different-process target HWND ready");DWORD pid=0;GetWindowThreadProcessId(target,&pid);
    check(pid==pi.dwProcessId&&pid!=GetCurrentProcessId(),"hit testing is genuinely cross process");
    HWND hud=create(L"YMCC_HudInputTestDisplay",hudProc,static_cast<DWORD>(ymcc::hudinput::requiredExStyle)|WS_EX_TOPMOST|WS_EX_NOREDIRECTIONBITMAP,WS_POPUP|WS_DISABLED,60,60,120,100);
    check(hud&&ymcc::hudinput::ensure(hud),"native display created disabled and transparent");
    check(ymcc::hudinput::registerTouchPassthrough(hud),"touch proximity exclusion registered on actual disabled HUD");
    for(int round=0;round<64;++round) {
        ShowWindow(hud,SW_SHOWNOACTIVATE);
        check(SetWindowPos(hud,HWND_TOPMOST,60,60,120+(round%3),100,SWP_NOACTIVATE|SWP_SHOWWINDOW)!=FALSE,"topmost non-activating geometry update");
        message(target,WM_APP+2); // Foreground request only inside the hidden desktop.
        const LONG_PTR removed=round%2?WS_EX_TRANSPARENT:WS_EX_NOACTIVATE;
        SetWindowLongPtrW(hud,GWL_EXSTYLE,GetWindowLongPtrW(hud,GWL_EXSTYLE)&~removed);
        EnableWindow(hud,TRUE); // Both actual Win32 notifications must repair synchronously.
        check(ymcc::hudinput::inert(hud),"style/enable changes cannot turn display into input surface");
        check((GetWindowLongPtrW(hud,GWL_EXSTYLE)&WS_EX_TOPMOST)!=0,"display remains topmost");
        const POINT point{80,80};const HWND hit=WindowFromPoint(point);
        check(hit==target,"actual Windows point hit test skips overlapping HUD into another process");
        check(GetForegroundWindow()!=hud,"HUD never becomes foreground");
        message(hit,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(40,40));
        message(hit,WM_LBUTTONUP,0,MAKELPARAM(40,40));
        ShowWindow(hud,SW_HIDE); // Alt-tab-like hide/show cycle; no visible desktop interaction.
    }
    check(visibleStyleLosses==0,"visible HUD never commits an input-blocking style, even before repair");
    check(message(target,WM_APP+1)==64,"point-routed button messages reached only underlying target");
    check(static_cast<LRESULT>(message(hud,WM_NCHITTEST,0,MAKELPARAM(80,80)))==HTTRANSPARENT,"fallback native hit-test result");
    check(message(hud,WM_MOUSEACTIVATE)==MA_NOACTIVATE,"no-activation mouse response");
    check(hudProc(hud,WM_POINTERACTIVATE,0,0)==PA_NOACTIVATE,"pointer cannot activate display");
    for(const POINT point : {POINT{80,80},POINT{59,59},POINT{180,160},POINT{60,80}}) {
        TOUCH_HIT_TESTING_INPUT input{};input.pointerId=1;input.point=point;
        input.boundingBox={point.x-8,point.y-8,point.x+8,point.y+8};input.nonOccludedBoundingBox=input.boundingBox;
        const TOUCH_HIT_TESTING_PROXIMITY_EVALUATION rejected{TOUCH_HIT_TESTING_PROXIMITY_FARTHEST,point};
        const auto expected=PackTouchHitTestingProximityEvaluation(&input,&rejected);
        check(hudProc(hud,WM_TOUCHHITTESTING,0,reinterpret_cast<LPARAM>(&input))==expected,"interior and edge touch hit tests explicitly reject proximity targeting");
    }
    DestroyWindow(hud);PostMessageW(target,WM_CLOSE,0,0);
    check(WaitForSingleObject(pi.hProcess,5000)==WAIT_OBJECT_0,"own helper exited cleanly");CloseHandle(pi.hProcess);
    check(SetThreadDesktop(original)!=FALSE,"restore only test thread desktop");CloseDesktop(desktop);
    std::printf("monitor input policy: %d assertions PASS; hidden desktop, 64 cross-process hit tests; no physical input or game acceptance\n",checks);
    return 0;
}
