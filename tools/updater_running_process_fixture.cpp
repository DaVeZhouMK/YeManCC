// Inert Win32 updater fixture. Readiness is emitted from the actual message
// pump; no hardware, product settings, driver, network or foreground control.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <string>
#include <fstream>
#include <filesystem>
static std::wstring readyPath,handshakePath,handshakeToken,imagePath;
static bool hangAfterReady=false;
static unsigned long long creationTicks(){FILETIME c{},e{},k{},u{};if(!GetProcessTimes(GetCurrentProcess(),&c,&e,&k,&u))return 0;return (static_cast<unsigned long long>(c.dwHighDateTime)<<32)|c.dwLowDateTime;}
static bool writeMarker(const std::wstring& file,const std::string& text){
    if(file.empty())return true;auto temporary=file+L".fixture-tmp";
    {std::ofstream out{std::filesystem::path(temporary),std::ios::binary};out<<text;out.flush();if(!out.good())return false;}
    return MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
}
static LRESULT CALLBACK windowProc(HWND hwnd,UINT message,WPARAM w,LPARAM l){
    if(message==WM_APP+1){
        auto identity=std::string("{\"phase\":\"ready\",\"pid\":")+std::to_string(GetCurrentProcessId())+",\"created\":\""+std::to_string(creationTicks())+"\",\"hwnd\":"+std::to_string(reinterpret_cast<unsigned long long>(hwnd))+"}";
        if(!writeMarker(readyPath,identity)){PostQuitMessage(4);return 0;}
        if(!handshakePath.empty()&&!std::filesystem::exists(std::filesystem::path(imagePath).parent_path()/L"fail-handshake.flag")){
            const auto text=std::string("{\"phase\":\"started\",\"pid\":")+std::to_string(GetCurrentProcessId())+",\"token\":\""+std::string(handshakeToken.begin(),handshakeToken.end())+"\"}";
            if(!writeMarker(handshakePath,text)){PostQuitMessage(5);return 0;}
        }
        if(hangAfterReady)Sleep(INFINITE);
        return 0;
    }
    if(message==WM_CLOSE){DestroyWindow(hwnd);return 0;}
    if(message==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(hwnd,message,w,l);
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
    int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);
    for(int i=1;argv&&i<argc;++i){const std::wstring arg=argv[i];
        if(arg==L"hung")hangAfterReady=true;
        else if(arg==L"--fixture-ready"&&i+1<argc)readyPath=argv[++i];
        else if(arg==L"--update-handshake"&&i+1<argc)handshakePath=argv[++i];
        else if(arg==L"--update-handshake-token"&&i+1<argc)handshakeToken=argv[++i];
    }
    if(argv)LocalFree(argv);wchar_t image[32768]{};GetModuleFileNameW(nullptr,image,32768);imagePath=image;
    WNDCLASSW wc{};wc.lpfnWndProc=windowProc;wc.hInstance=instance;wc.lpszClassName=L"YMCCUpdaterInertFixture";
    if(!RegisterClassW(&wc))return 2;
    HWND hwnd=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,wc.lpszClassName,L"YMCC updater fixture",WS_POPUP,-32000,-32000,1,1,nullptr,nullptr,instance,nullptr);
    if(!hwnd)return 3;
    // An off-screen 1px tool window is discoverable for a real graceful close,
    // without focus or a taskbar item. Start-Process still requests SW_HIDE.
    ShowWindow(hwnd,SW_SHOWNOACTIVATE);PostMessageW(hwnd,WM_APP+1,0,0);
    MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){TranslateMessage(&message);DispatchMessageW(&message);}return static_cast<int>(message.wParam);
}
