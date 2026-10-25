#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <taskschd.h>
#include <appmodel.h>
#include <sddl.h>
#include <cwctype>
#include <wrl/client.h>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>
#include <stdexcept>
#include "flow.h"
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace gh = ymcc::gaminghome;
namespace {
struct Handle { HANDLE h=nullptr; ~Handle(){ if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h); } };
struct Bstr { BSTR s; explicit Bstr(const wchar_t* v):s(SysAllocString(v)){} ~Bstr(){SysFreeString(s);} operator BSTR() const{return s;} };
std::wstring lower(std::wstring x) { std::transform(x.begin(),x.end(),x.begin(),[](wchar_t c){return wchar_t(towlower(c));}); return x; }
std::wstring image(DWORD pid) {
    Handle p{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};
    if(!p.h) return {};
    wchar_t text[32768]; DWORD size=32768;
    return QueryFullProcessImageNameW(p.h,0,text,&size) ? lower(std::wstring(text,size)) : L"";
}
bool sameSession(DWORD pid) { DWORD a=0,b=0; return ProcessIdToSessionId(GetCurrentProcessId(),&a) && ProcessIdToSessionId(pid,&b) && a==b; }
bool ymccPresent() {
    Handle snap{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    if(snap.h==INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W p{sizeof(p)};
    for(BOOL more=Process32FirstW(snap.h,&p);more;more=Process32NextW(snap.h,&p))
        if(_wcsicmp(p.szExeFile,L"YeManCC.exe")==0 && sameSession(p.th32ProcessID) && image(p.th32ProcessID)==lower(gh::Ymcc)) return true;
    return false;
}
std::wstring registryString(HKEY key,const wchar_t* path,const wchar_t* name) {
    wchar_t text[32768]; DWORD bytes=sizeof(text);
    if(RegGetValueW(key,path,name,RRF_RT_REG_SZ,nullptr,text,&bytes)!=ERROR_SUCCESS) return {};
    return text;
}
bool selectedHome() {
    UINT32 size=0;
    if(GetCurrentPackageFamilyName(&size,nullptr)!=ERROR_INSUFFICIENT_BUFFER) return false;
    std::wstring name(size,L'\0');
    if(GetCurrentPackageFamilyName(&size,name.data())!=ERROR_SUCCESS) return false;
    name.resize(size-1);
    return registryString(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\GamingConfiguration",L"GamingHomeApp")==name+L"!Home";
}
bool principalIsCurrentUser(const wchar_t* user) {
    if(!user || !*user) return false;
    Handle token;
    if(!OpenProcessToken(GetCurrentProcess(),TOKEN_QUERY,&token.h)) return false;
    DWORD bytes=0;GetTokenInformation(token.h,TokenUser,nullptr,0,&bytes);
    std::vector<BYTE> info(bytes);
    if(!GetTokenInformation(token.h,TokenUser,info.data(),bytes,&bytes)) return false;
    PSID sid=nullptr;
    if(ConvertStringSidToSidW(user,&sid)) {
        const bool equal=EqualSid(sid,reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid)!=FALSE;
        LocalFree(sid);return equal;
    }
    DWORD sidBytes=0,domainChars=0;SID_NAME_USE use{};
    LookupAccountNameW(nullptr,user,nullptr,&sidBytes,nullptr,&domainChars,&use);
    if(!sidBytes)return false;
    std::vector<BYTE> sidData(sidBytes);std::vector<wchar_t> domain(domainChars);
    return LookupAccountNameW(nullptr,user,sidData.data(),&sidBytes,domain.data(),&domainChars,&use) &&
        EqualSid(sidData.data(),reinterpret_cast<TOKEN_USER*>(info.data())->User.Sid);
}
std::wstring steamRoot() {
    auto exe=registryString(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamExe");
    if(!exe.empty()) return lower(fs::path(exe).parent_path().lexically_normal().wstring());
    auto root=registryString(HKEY_LOCAL_MACHINE,L"SOFTWARE\\WOW6432Node\\Valve\\Steam",L"InstallPath");
    return lower(fs::path(root).lexically_normal().wstring());
}
void logEvent(const char* event,const std::wstring& detail=L"") noexcept {
    try {
        PWSTR value=nullptr;
        if(FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData,0,nullptr,&value))) return;
        const fs::path root=fs::path(value)/L"YeManCC"; CoTaskMemFree(value);
        std::error_code ec;fs::create_directories(root,ec);
        // Reuse the existing ordinary YMCC export entry. Never truncate other
        // launch receipts; stop appending once the shared file reaches 4 MiB.
        const auto file=root/L"steam-launch.log";
        if(fs::exists(file,ec) && fs::file_size(file,ec)>4*1024*1024) return;
        SYSTEMTIME t{};GetLocalTime(&t);
        wchar_t prefix[200];swprintf_s(prefix,L"[%04u-%02u-%02u %02u:%02u:%02u.%03u] [GamingHome pid=%lu] ",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,t.wMilliseconds,GetCurrentProcessId());
        std::wstring line=prefix;for(const char* c=event;*c;++c)line.push_back(*c);line+=L" "+detail+L"\r\n";
        int n=WideCharToMultiByte(CP_UTF8,0,line.data(),int(line.size()),nullptr,0,nullptr,nullptr);
        std::string bytes(n,'\0');WideCharToMultiByte(CP_UTF8,0,line.data(),int(line.size()),bytes.data(),n,nullptr,nullptr);
        Handle h{CreateFileW(file.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr)};
        if(h.h!=INVALID_HANDLE_VALUE){DWORD written=0;WriteFile(h.h,bytes.data(),DWORD(bytes.size()),&written,nullptr);}
    } catch(...) {}
}
LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_PAINT) {
        PAINTSTRUCT ps; HDC dc=BeginPaint(hwnd,&ps);RECT r;GetClientRect(hwnd,&r);
        FillRect(dc,&r,static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        SetTextColor(dc,RGB(235,235,235));SetBkMode(dc,TRANSPARENT);
        SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
        DrawTextW(dc,L"YMCC\n正在打开 Steam 大屏…",-1,&r,DT_CENTER|DT_VCENTER|DT_WORDBREAK);
        EndPaint(hwnd,&ps);return 0;
    }
    if(msg==WM_CLOSE){DestroyWindow(hwnd);return 0;}
    return DefWindowProcW(hwnd,msg,w,l);
}
class WindowsPorts final:public gh::Ports {
    using Flag=BOOL(WINAPI*)();
    HMODULE api_=nullptr;Flag supported_=nullptr,active_=nullptr;
    Handle mutex_; HWND splash_=nullptr,steam_=nullptr;
    std::wstring root_;
    bool cancelled_=false;
    struct WindowSearch { WindowsPorts* self;HWND found=nullptr; };
    bool steamWindow(HWND hwnd) {
        if(!IsWindow(hwnd)||!IsWindowVisible(hwnd)||hwnd==splash_)return false;
        wchar_t cls[128]{};GetClassNameW(hwnd,cls,128);
        if(wcscmp(cls,L"SDL_app"))return false;
        DWORD pid=0;GetWindowThreadProcessId(hwnd,&pid);
        if(!sameSession(pid)||root_.empty())return false;
        const auto path=image(pid);
        const auto cef=root_+L"\\bin\\cef\\";
        const bool owned=path==root_+L"\\steam.exe" || (path.starts_with(cef) && fs::path(path).filename()==L"steamwebhelper.exe");
        RECT r{};if(!GetWindowRect(hwnd,&r))return false;
        return gh::windowCandidate(true,true,owned,true,
            (GetWindowLongPtrW(hwnd,GWL_EXSTYLE)&WS_EX_APPWINDOW)!=0,
            (GetWindowLongPtrW(hwnd,GWL_STYLE)&WS_THICKFRAME)!=0,r.right-r.left,r.bottom-r.top);
    }
    static BOOL CALLBACK enumerate(HWND hwnd,LPARAM p) {
        auto& search=*reinterpret_cast<WindowSearch*>(p);
        if(search.self->steamWindow(hwnd)){search.found=hwnd;return FALSE;}return TRUE;
    }
    void ensureSplash() {
        if(splash_||cancelled_)return;
        WNDCLASSW c{};c.hInstance=GetModuleHandleW(nullptr);c.lpfnWndProc=windowProc;c.lpszClassName=L"YMCCGamingHome";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);
        RegisterClassW(&c);
        MONITORINFO mi{sizeof(mi)};GetMonitorInfoW(MonitorFromPoint({0,0},MONITOR_DEFAULTTOPRIMARY),&mi);
        const auto& r=mi.rcMonitor;
        splash_=CreateWindowExW(0,c.lpszClassName,L"YMCC Steam 全屏主页",WS_POPUP|WS_VISIBLE,r.left,r.top,r.right-r.left,r.bottom-r.top,nullptr,nullptr,c.hInstance,nullptr);
        if(splash_)UpdateWindow(splash_);
    }
    bool shell(const wchar_t* file,const wchar_t* directory=nullptr) {
        SHELLEXECUTEINFOW s{sizeof(s)};s.fMask=SEE_MASK_NOCLOSEPROCESS|SEE_MASK_NOASYNC|SEE_MASK_FLAG_NO_UI;
        s.lpVerb=L"open";s.lpFile=file;s.lpDirectory=directory;s.nShow=SW_HIDE;
        const bool ok=ShellExecuteExW(&s)!=FALSE;if(s.hProcess)CloseHandle(s.hProcess);
        if(!ok)logEvent("shell-failed",L"win32="+std::to_wstring(GetLastError()));return ok;
    }
public:
    WindowsPorts() {
        api_=LoadLibraryExW(L"api-ms-win-gaming-experience-l1-1-0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(api_){supported_=reinterpret_cast<Flag>(GetProcAddress(api_,"IsGamingFullScreenExperienceSupported"));active_=reinterpret_cast<Flag>(GetProcAddress(api_,"IsGamingFullScreenExperienceActive"));}
        root_=steamRoot();
    }
    ~WindowsPorts(){if(IsWindow(splash_))DestroyWindow(splash_);if(api_)FreeLibrary(api_);}
    bool available() override {return supported_&&active_&&supported_();}
    bool selectedHome() override {return ::selectedHome();}
    bool lock() override {
        mutex_.h=CreateMutexW(nullptr,FALSE,L"Local\\YeManCC.GamingHome.Activation.v1");
        return mutex_.h&&GetLastError()!=ERROR_ALREADY_EXISTS;
    }
    bool files() override {return fs::is_regular_file(gh::Bat)&&fs::is_regular_file(gh::Ymcc);}
    gh::Snapshot snapshot() override {
        if(splash_&&!IsWindow(splash_))cancelled_=true;
        bool active=active_&&active_()&&::selectedHome();
        if(active&&!cancelled_)ensureSplash();
        HWND foreground=GetForegroundWindow();
        if(steamWindow(foreground))steam_=foreground;
        else {WindowSearch s{this};EnumWindows(enumerate,reinterpret_cast<LPARAM>(&s));steam_=s.found;}
        return {active,ymccPresent(),steam_!=nullptr,cancelled_};
    }
    bool startYmccTask() override {
        ComPtr<ITaskService> service;HRESULT h=CoCreateInstance(CLSID_TaskScheduler,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&service));
        VARIANT empty;VariantInit(&empty);
        if(SUCCEEDED(h))h=service->Connect(empty,empty,empty,empty);
        ComPtr<ITaskFolder> folder;if(SUCCEEDED(h))h=service->GetFolder(Bstr(L"\\"),&folder);
        ComPtr<IRegisteredTask> task;if(SUCCEEDED(h))h=folder->GetTask(Bstr(gh::Task),&task);
        ComPtr<ITaskDefinition> def;if(SUCCEEDED(h))h=task->get_Definition(&def);
        ComPtr<IRegistrationInfo> reg;if(SUCCEEDED(h))h=def->get_RegistrationInfo(&reg);
        BSTR description=nullptr;if(SUCCEEDED(h))h=reg->get_Description(&description);
        bool owned=SUCCEEDED(h)&&description&&wcscmp(description,gh::TaskOwner)==0;SysFreeString(description);
        ComPtr<IActionCollection> actions;if(owned)h=def->get_Actions(&actions);else h=E_ACCESSDENIED;
        LONG count=0;if(SUCCEEDED(h))h=actions->get_Count(&count);
        ComPtr<IAction> action;if(SUCCEEDED(h)&&count==1)h=actions->get_Item(1,&action);else h=E_ACCESSDENIED;
        ComPtr<IExecAction> exe;if(SUCCEEDED(h))h=action.As(&exe);
        BSTR path=nullptr,args=nullptr,directory=nullptr;
        if(SUCCEEDED(h))h=exe->get_Path(&path);if(SUCCEEDED(h))h=exe->get_Arguments(&args);
        if(SUCCEEDED(h))h=exe->get_WorkingDirectory(&directory);
        owned=SUCCEEDED(h)&&path&&_wcsicmp(path,gh::Ymcc)==0&&args&&wcscmp(args,L"--minimized")==0&&directory&&_wcsicmp(directory,L"C:\\SOFT\\YeMan\\YeManCC")==0;
        SysFreeString(path);SysFreeString(args);SysFreeString(directory);
        // The elevated task is demand-only for this exact user and action.
        // Revalidate before Run; no trigger, SYSTEM account or different path.
        ComPtr<IPrincipal> principal;
        if(owned)h=def->get_Principal(&principal);else h=E_ACCESSDENIED;
        BSTR user=nullptr;TASK_RUNLEVEL_TYPE level=TASK_RUNLEVEL_LUA;TASK_LOGON_TYPE logon=TASK_LOGON_NONE;
        if(SUCCEEDED(h))h=principal->get_UserId(&user);
        if(SUCCEEDED(h))h=principal->get_RunLevel(&level);
        if(SUCCEEDED(h))h=principal->get_LogonType(&logon);
        owned=SUCCEEDED(h)&&principalIsCurrentUser(user)&&level==TASK_RUNLEVEL_HIGHEST&&logon==TASK_LOGON_INTERACTIVE_TOKEN;
        SysFreeString(user);
        ComPtr<ITriggerCollection> triggers;LONG triggerCount=-1;
        if(owned)h=def->get_Triggers(&triggers);else h=E_ACCESSDENIED;
        if(SUCCEEDED(h))h=triggers->get_Count(&triggerCount);
        owned=SUCCEEDED(h)&&triggerCount==0;
        ComPtr<IRunningTask> running;if(owned)h=task->Run(empty,&running);else h=E_ACCESSDENIED;
        logEvent("ymcc-task-result",L"hresult="+std::to_wstring(static_cast<unsigned long>(h)));
        return SUCCEEDED(h);
    }
    bool startBat() override {return shell(gh::Bat,L"C:\\SOFT\\YeMan\\PowerControl");}
    bool showSteam() override {return shell(gh::SteamUri);}
    bool focusSteam() override {
        if(!steamWindow(steam_))return false;
        if(IsIconic(steam_))ShowWindow(steam_,SW_RESTORE);
        SetForegroundWindow(steam_);
        if(GetForegroundWindow()!=steam_)return false;
        DWORD pid=0;GetWindowThreadProcessId(steam_,&pid);
        Handle p{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid)};FILETIME birth{},exit{},kernel{},user{};
        if(!p.h||!GetProcessTimes(p.h,&birth,&exit,&kernel,&user))return false;
        const auto created=(static_cast<unsigned long long>(birth.dwHighDateTime)<<32)|birth.dwLowDateTime;
        logEvent("handoff-window-identity",L"pid="+std::to_wstring(pid)+L" birth="+std::to_wstring(created)+L" hwnd="+std::to_wstring(reinterpret_cast<UINT_PTR>(steam_))+L" image="+image(pid)+L" mode=unobserved");
        return true;
    }
    std::uint64_t now() override{return GetTickCount64();}
    void wait() override {
        MsgWaitForMultipleObjectsEx(0,nullptr,200,QS_ALLINPUT,MWMO_INPUTAVAILABLE);
        MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_QUIT)cancelled_=true;TranslateMessage(&m);DispatchMessageW(&m);}
    }
    void event(const char* text) override{logEvent(text);}
};
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,LPWSTR,int) {
    int count=0;LPWSTR* args=CommandLineToArgvW(GetCommandLineW(),&count);
    const bool activate=args&&((count==2&&gh::activation(args[1])) || (count==1&&selectedHome()));
    if(args)LocalFree(args);
    if(!activate){MessageBoxW(nullptr,L"YMCC Steam 全屏主页组件\n\n请通过启用入口注册，然后在 Windows 全屏主页选择 YMCC。\n此组件使用原 YeManSteam.bat，不修改 .earlystart。",L"YMCC",MB_OK);return 0;}
    HRESULT co=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
    gh::Result result=gh::Result::InternalError;
    try {if(FAILED(co))throw std::runtime_error("COM initialization");logEvent("activation");WindowsPorts ports;result=gh::run(ports);}
    catch(...){logEvent("unhandled-error");}
    logEvent("complete",L"result="+std::to_wstring(static_cast<int>(result)));
    if(SUCCEEDED(co))CoUninitialize();
    if(result!=gh::Result::Success&&result!=gh::Result::Busy&&result!=gh::Result::Cancelled)
        MessageBoxW(nullptr,gh::message(result),L"YMCC 全屏主页",MB_OK|MB_ICONWARNING);
    return result==gh::Result::Success||result==gh::Result::Busy||result==gh::Result::Cancelled?0:1;
}

