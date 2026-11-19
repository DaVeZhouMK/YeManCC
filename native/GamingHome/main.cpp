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
#include "../steam_information_client.h"
#include "../steam_client_action_channel.h"
#include "../steam_home_cancel_messages.h"
using Microsoft::WRL::ComPtr;
namespace fs = std::filesystem;
namespace gh = ymcc::gaminghome;
namespace {
struct Handle {
    HANDLE h=nullptr;
    explicit Handle(HANDLE value=nullptr):h(value){}
    ~Handle(){ reset(); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    void reset(HANDLE value=nullptr) { if(h && h!=INVALID_HANDLE_VALUE) CloseHandle(h); h=value; }
};
struct Bstr { BSTR s; explicit Bstr(const wchar_t* v):s(SysAllocString(v)){} ~Bstr(){SysFreeString(s);} operator BSTR() const{return s;} };
std::wstring lower(std::wstring x) { std::transform(x.begin(),x.end(),x.begin(),[](wchar_t c){return wchar_t(towlower(c));}); return x; }
std::wstring processImage(HANDLE process) {
    if(!process) return {};
    wchar_t text[32768]; DWORD size=32768;
    return QueryFullProcessImageNameW(process,0,text,&size) ? lower(std::wstring(text,size)) : L"";
}
bool sameSession(DWORD pid) { DWORD a=0,b=0; return ProcessIdToSessionId(GetCurrentProcessId(),&a) && ProcessIdToSessionId(pid,&b) && a==b; }
HANDLE findYmccProcess() {
    Handle snap{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
    if(snap.h==INVALID_HANDLE_VALUE) return nullptr;
    PROCESSENTRY32W p{sizeof(p)};
    for(BOOL more=Process32FirstW(snap.h,&p);more;more=Process32NextW(snap.h,&p)) {
        if(_wcsicmp(p.szExeFile,L"YeManCC.exe")!=0 || !sameSession(p.th32ProcessID)) continue;
        Handle process{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,p.th32ProcessID)};
        if(process.h && processImage(process.h)==lower(gh::Ymcc) && WaitForSingleObject(process.h,0)==WAIT_TIMEOUT) {
            HANDLE result=process.h;process.h=nullptr;return result;
        }
    }
    return nullptr;
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
gh::SteamPresence homeSteamPresence(const ymcc::steaminfo::Facts& source) noexcept {
    return gh::steamPresence(source.available,source.startupWindow.completed,source.presence,source.processPid,source.processBirth);
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
    Handle mutex_,ymcc_; HWND splash_=nullptr;
    DWORD ymccPid_=0;
    std::uint64_t ymccBirth_=0;
    ymcc::steaminfo::Facts sourceFacts_;
    std::uint64_t activationBegin_=0;
    bool unknown_=false,commandBusy_=false;
    bool cancelled_=false;

    bool ownerLive() const noexcept {
        return ymcc_.h&&ymccPid_&&ymccBirth_&&WaitForSingleObject(ymcc_.h,0)==WAIT_TIMEOUT;
    }

    void captureOwner() {
        if(!ymcc_.h){
            ymcc_.reset(findYmccProcess());
            if(!ymcc_.h)return;
            FILETIME birth{},exit{},kernel{},cpu{};
            ymccPid_=GetProcessId(ymcc_.h);
            if(GetProcessTimes(ymcc_.h,&birth,&exit,&kernel,&cpu))
                ymccBirth_=(std::uint64_t(birth.dwHighDateTime)<<32)|birth.dwLowDateTime;
        }
        if(!ownerLive())cancelled_=true;
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

public:
    WindowsPorts() {
        api_=LoadLibraryExW(L"api-ms-win-gaming-experience-l1-1-0.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(api_){supported_=reinterpret_cast<Flag>(GetProcAddress(api_,"IsGamingFullScreenExperienceSupported"));active_=reinterpret_cast<Flag>(GetProcAddress(api_,"IsGamingFullScreenExperienceActive"));}

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
        if(!active)cancelled_=true;
        if(active&&!cancelled_)ensureSplash();
        sourceFacts_={};
        if(active&&!cancelled_)captureOwner();
        if(active&&!cancelled_&&ymcc_.h) {
            sourceFacts_=ymcc::steaminfo::readHomeStartupWindow(ymccPid_,ymccBirth_);
            if(!ownerLive()){cancelled_=true;sourceFacts_={};}
        }
        return {active,!cancelled_&&ymcc_.h,sourceFacts_.available&&sourceFacts_.startupWindow.known,cancelled_,homeSteamPresence(sourceFacts_)};
    }
    bool current(gh::Action action) override {
        if(ymcc::steamaction::homeCancelPending(splash_))cancelled_=true;
        if(splash_&&!IsWindow(splash_))cancelled_=true;
        if(cancelled_ || !(active_&&active_()) || !::selectedHome()) {cancelled_=true;return false;}
        switch(action) {
        case gh::Action::EnsureYmcc: return true;
        case gh::Action::StartSteam:
        case gh::Action::ShowHome:
        case gh::Action::FocusWindow:
            if(!ymcc_.h) return false;
            if(!ownerLive()) {cancelled_=true;return false;}
            return true;
        }
        return false;
    }
    bool dispatch(gh::Action action,const gh::Current& current) override {
        switch(action) {
        case gh::Action::EnsureYmcc: return startYmccTask(current);
        case gh::Action::StartSteam:
        case gh::Action::ShowHome:
        case gh::Action::FocusWindow: return requestSteam(action,current);
        }
        return false;
    }
private:
    bool startYmccTask(const gh::Current& current) {
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
        ComPtr<IRunningTask> running;
        if(owned) {
            if(!current()) return false;
            h=task->Run(empty,&running);
        } else h=E_ACCESSDENIED;
        logEvent("ymcc-task-result",L"hresult="+std::to_wstring(static_cast<unsigned long>(h)));
        return SUCCEEDED(h);
    }
    bool requestSteam(gh::Action action,const gh::Current& current) {
        namespace sa=ymcc::steamaction;
        unknown_=false;commandBusy_=false;
        if(!current()||!ymcc_.h||!activationBegin_||!sourceFacts_.available||!sourceFacts_.actionPowerGeneration)return false;
        const auto presence=homeSteamPresence(sourceFacts_);
        if((action==gh::Action::StartSteam&&presence!=gh::SteamPresence::Absent)||
           (action==gh::Action::ShowHome&&presence!=gh::SteamPresence::Running))return false;
        const auto now=GetTickCount64(),end=activationBegin_+gh::DeadlineMs;
        if(now<activationBegin_||now>=end)return false;
        sa::Request request;request.version=sa::homeProtocol;
        request.command=static_cast<std::uint32_t>(action==gh::Action::StartSteam?sa::Command::HomeStart:
            action==gh::Action::ShowHome?sa::Command::HomeShow:sa::Command::HomeFocus);
        request.deadline=(std::min)(end,now+sa::requestBudgetMs);
        request.powerGeneration=sourceFacts_.actionPowerGeneration;request.powerSequence=sourceFacts_.actionPowerSequence;
        request.powerSerial=sourceFacts_.actionPowerSerial;
        if(action!=gh::Action::StartSteam){
            if(sourceFacts_.presence!=1||!sourceFacts_.processPid||!sourceFacts_.processBirth)return false;
            request.targetPid=sourceFacts_.processPid;request.targetBirth=sourceFacts_.processBirth;
        }
        const auto result=sa::submit(request,current,ymccPid_,ymccBirth_);
        unknown_=result==sa::Outcome::Unknown;commandBusy_=result==sa::Outcome::Busy;
        logEvent("public-steam-action",L"command="+std::to_wstring(request.command)+L" outcome="+std::to_wstring(static_cast<LONG>(result)));
        return result==sa::Outcome::Submitted;
    }

public:
    void begin(std::uint64_t value) override{activationBegin_=value;}
    bool uncertain()const override{return unknown_;}
    bool commandBusy()const override{return commandBusy_;}
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

