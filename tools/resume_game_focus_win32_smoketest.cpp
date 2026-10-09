// Only an owned synthetic fixture process/window is exercised. No real game,
// NtSuspendProcess, sleep, input injection, or driver operation is performed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include "resume_focus.h"

struct Shared { volatile LONG_PTR window; };
static HANDLE enteredEvent=nullptr, releaseEvent=nullptr;
static std::atomic<int> heartbeats{0};
static LRESULT CALLBACK fixtureProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_APP+1) { SetEvent(enteredEvent); WaitForSingleObject(releaseEvent,5000); return 0; }
    if(m==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(h,m,w,l);
}
static LRESULT CALLBACK heartbeatProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_TIMER){heartbeats.fetch_add(1);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static void pump() { MSG msg;while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);} }
static int fixture(const std::wstring& name) {
    HANDLE mapping=OpenFileMappingW(FILE_MAP_ALL_ACCESS,FALSE,name.c_str());
    auto* shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
    enteredEvent=OpenEventW(EVENT_MODIFY_STATE,FALSE,(name+L"-entered").c_str());
    releaseEvent=OpenEventW(SYNCHRONIZE,FALSE,(name+L"-release").c_str());
    if(!shared||!enteredEvent||!releaseEvent)return 2;
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=fixtureProc;cls.lpszClassName=L"YmccResumeFocusOwnedFixture";
    RegisterClassW(&cls);
    // No-activate tool window, off screen; never masquerades as a game or uses
    // a real user application's thread/input queue.
    HWND h=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,cls.lpszClassName,L"YMCC focus fixture",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,-32000,-32000,80,80,nullptr,nullptr,cls.hInstance,nullptr);
    InterlockedExchangePointer(reinterpret_cast<void* volatile*>(&shared->window),h);
    MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}
    UnmapViewOfFile(shared);CloseHandle(mapping);CloseHandle(enteredEvent);CloseHandle(releaseEvent);return 0;
}
int wmain(int argc,wchar_t** argv) {
    if(argc==3&&std::wstring(argv[1])==L"--fixture")return fixture(argv[2]);
    const std::wstring name=L"Local\\YMCC-Focus-SelfTest-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64());
    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,sizeof(Shared),name.c_str());
    auto* shared=static_cast<Shared*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,sizeof(Shared)));
    enteredEvent=CreateEventW(nullptr,TRUE,FALSE,(name+L"-entered").c_str());
    releaseEvent=CreateEventW(nullptr,TRUE,FALSE,(name+L"-release").c_str());
    if(!shared||!enteredEvent||!releaseEvent)return 2;
    wchar_t executable[MAX_PATH]{};GetModuleFileNameW(nullptr,executable,MAX_PATH);
    std::wstring command=L"\""+std::wstring(executable)+L"\" --fixture \""+name+L"\"";
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi))return 3;
    const ULONGLONG start=GetTickCount64();
    while(!shared->window&&GetTickCount64()-start<3000)Sleep(10);
    HWND target=reinterpret_cast<HWND>(shared->window);
    bool ok=target!=nullptr;
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=heartbeatProc;cls.lpszClassName=L"YmccResumeFocusHeartbeat";RegisterClassW(&cls);
    HWND ui=CreateWindowExW(0,cls.lpszClassName,L"",0,0,0,0,0,HWND_MESSAGE,nullptr,cls.hInstance,nullptr);
    SetTimer(ui,1,20,nullptr);
    auto bounded=[&](bool activate,bool expectedResponsive,const char* label){
        std::atomic<bool> done{false};bool responsive=false;ULONGLONG elapsed=0;
        const int before=heartbeats.load();
        std::thread worker([&]{const auto tick=GetTickCount64();
            responsive=ymcc::resumeWindowResponds(target);
            if(activate)ymcc::requestWindowForegroundAsync(target);
            elapsed=GetTickCount64()-tick;done.store(true,std::memory_order_release);
        });
        const auto tick=GetTickCount64();
        while(!done.load(std::memory_order_acquire)&&GetTickCount64()-tick<1000){pump();Sleep(5);}
        const bool boundedCompletion=done.load(std::memory_order_acquire);
        if(!boundedCompletion)SetEvent(releaseEvent); // only release our own synthetic window
        worker.join();pump();
        const int ticks=heartbeats.load()-before;
        const bool pass=boundedCompletion&&elapsed<1000&&responsive==expectedResponsive&&(!expectedResponsive?ticks>0:true);
        std::cout<<(pass?"PASS ":"FAIL ")<<label<<" elapsedMs="<<elapsed<<" uiHeartbeatTicks="<<ticks<<" responsive="<<responsive<<"\n";
        ok=ok&&pass;
    };
    if(ok){
        bounded(false,true,"responsive fixture probe");
        PostMessageW(target,WM_APP+1,0,0);
        ok=ok&&WaitForSingleObject(enteredEvent,2000)==WAIT_OBJECT_0;
        bounded(false,false,"hung fixture times out while UI keeps pumping");
        // Stress the shared helper directly, even though production does NOT
        // activate failed probes. Foreign show/z-order/foreground stay async.
        bounded(true,false,"async focus on hung fixture cannot couple UI queues");
        SetEvent(releaseEvent);Sleep(30);
        bounded(false,true,"fixture responsive again after owned release");
    }
    SetEvent(releaseEvent);if(target)PostMessageW(target,WM_CLOSE,0,0);
    DWORD exitWait=WaitForSingleObject(pi.hProcess,3000);
    if(exitWait!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,9);ok=false;} // exclusively the child created above
    // The window handle is now invalid; the same shared production probe fails safely.
    if(ymcc::resumeWindowResponds(target)){std::cout<<"FAIL exited fixture probe\n";ok=false;}
    else std::cout<<"PASS exited fixture window is skipped\n";
    KillTimer(ui,1);DestroyWindow(ui);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);
    UnmapViewOfFile(shared);CloseHandle(mapping);CloseHandle(enteredEvent);CloseHandle(releaseEvent);
    std::cout<<"owned Win32 resume-focus fixture: "<<(ok?"PASS":"FAIL")<<" (no sleep/real game touched)\n";
    return ok?0:1;
}
