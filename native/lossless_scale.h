#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <UIAutomation.h>
#include <wrl/client.h>
#include <functional>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <cwctype>
#include "lossless_launch_contract.h"
#include "lossless_scale_contract.h"
#pragma comment(lib,"uiautomationcore.lib")
#pragma comment(lib,"ole32.lib")
#pragma comment(lib,"oleaut32.lib")
namespace ymcc::lossless {
inline std::wstring scaleTrim(std::wstring s){
    const auto a=s.find_first_not_of(L" \t\r\n"),b=s.find_last_not_of(L" \t\r\n");
    return a==std::wstring::npos?std::wstring{}:s.substr(a,b-a+1);
}
inline ScaleButton scaleButtonState(const std::wstring& raw){
    const auto s=scaleTrim(raw);
    for(const auto* on:{L"Unscale",L"Stop scaling",L"停止缩放",L"停止縮放",L"取消缩放",L"取消縮放",L"无缩放",L"無縮放"})if(s==on)return ScaleButton::On;
    for(const auto* off:{L"Scale",L"缩放",L"開始縮放",L"开始缩放"})if(s==off)return ScaleButton::Off;
    if(!s.empty()&&s.size()<=4&&s.find_first_not_of(L"0123456789")==std::wstring::npos)return ScaleButton::Countdown;
    return ScaleButton::Unknown;
}
inline bool scaleProfileMatches(const std::wstring& status,const std::wstring& title){
    if(title.empty())return false;
    std::wistringstream lines(status);std::wstring line;
    for(int i=0;i<3;i++)if(!std::getline(lines,line))return false;
    const auto colon=line.find_first_of(L":：");if(colon==std::wstring::npos)return false;
    const auto receipt=scaleTrim(line.substr(colon+1));
    return receipt==L"\""+title+L"\""||receipt==L"“"+title+L"”"||receipt==L"«"+title+L"»";
}
inline bool scalePaused(const std::wstring& status){
    std::wistringstream lines(status);std::wstring line;unsigned nonempty=0;
    while(std::getline(lines,line))if(!scaleTrim(line).empty()&&++nonempty>3)return true;
    return false;
}
inline bool scaleAbsolute(std::wstring& s){
    if(s.size()<4||s.size()>32767||s.find(L'\0')!=std::wstring::npos||s.find(L'"')!=std::wstring::npos||
       !iswalpha(s[0])||s[1]!=L':'||(s[2]!=L'\\'&&s[2]!=L'/'))return false;
    std::vector<wchar_t> full(32768);const auto count=GetFullPathNameW(s.c_str(),static_cast<DWORD>(full.size()),full.data(),nullptr);
    if(!count||count>=full.size())return false;s.assign(full.data(),count);return true;
}
inline Identity scaleIdentity(std::wstring exe,std::uint64_t pid,const std::string& created){
    if(!pid||pid>MAXDWORD||created.empty()||created.size()>20||created.find_first_not_of("0123456789")!=std::string::npos)
        throw std::runtime_error("LOSSLESS_INVALID_RUNTIME_REQUEST");
    const auto birth=std::stoull(created);if(!birth||!scaleAbsolute(exe))throw std::runtime_error("LOSSLESS_INVALID_RUNTIME_REQUEST");
    return {static_cast<DWORD>(pid),birth,std::move(exe)};
}
class ScaleProcess {
    HANDLE process_=nullptr;DWORD openError_=0;
public:
    ScaleProcess()=default;ScaleProcess(const ScaleProcess&)=delete;ScaleProcess& operator=(const ScaleProcess&)=delete;
    ~ScaleProcess(){if(process_)CloseHandle(process_);}
    bool open(const Identity& expected,bool requireLossless=true,DWORD extraRights=0){
        std::wstring path=expected.path;
        if(!expected.pid||!expected.created||!scaleAbsolute(path))return false;
        const auto slash=path.find_last_of(L"\\/");
        if(slash==std::wstring::npos||(requireLossless&&_wcsicmp(path.c_str()+slash+1,L"LosslessScaling.exe")))return false;
        process_=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE|extraRights,FALSE,expected.pid);
        if(!process_){openError_=GetLastError();return false;}
        DWORD a=0,b=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&a)||!ProcessIdToSessionId(expected.pid,&b)||a!=b)return false;
        std::vector<wchar_t> image(32768);DWORD size=static_cast<DWORD>(image.size());FILETIME c{},e{},k{},u{};
        if(!QueryFullProcessImageNameW(process_,0,image.data(),&size)||!GetProcessTimes(process_,&c,&e,&k,&u))return false;
        const auto created=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
        return alive()&&created==expected.created&&_wcsicmp(path.c_str(),std::wstring(image.data(),size).c_str())==0;
    }
    bool alive()const{return process_&&WaitForSingleObject(process_,0)==WAIT_TIMEOUT;}
    HANDLE handle()const{return process_;}
    DWORD openError()const{return openError_;}
};
// Explicit failure reset only; no process-name sweep, child-tree kill or retry.
inline ScaleFailureReset terminateFailedLossless(const Identity& expected){
    ScaleFailureReset r;r.requested=true;
    if(!expected.pid||expected.pid==4||expected.pid==GetCurrentProcessId()){r.reason="failure-reset-identity-rejected";return r;}
    ScaleProcess victim;
    if(!victim.open(expected,true,PROCESS_TERMINATE)){r.error=victim.openError();r.closed=r.error==ERROR_INVALID_PARAMETER;r.reason=r.closed?"failure-reset-already-exited":"failure-reset-identity-unconfirmed";return r;}
    if(!victim.alive()){r.closed=true;r.reason="failure-reset-already-exited";return r;}
    r.attempted=true;
    if(!TerminateProcess(victim.handle(),1)){r.error=GetLastError();r.reason="failure-reset-terminate-denied";return r;}
    r.terminated=true;r.closed=WaitForSingleObject(victim.handle(),800)==WAIT_OBJECT_0;
    r.reason=r.closed?"failure-reset-closed":"failure-reset-exit-unconfirmed";return r;
}
inline bool scaleMain(HWND h,DWORD pid){
    DWORD owner=0;GetWindowThreadProcessId(h,&owner);if(owner!=pid)return false;
    wchar_t title[512]={},cls[512]={};GetWindowTextW(h,title,512);GetClassNameW(h,cls,512);
    return std::wstring(title)==L"Lossless Scaling"&&std::wstring(cls).rfind(L"HwndWrapper[LosslessScaling;",0)==0;
}
inline HWND scaleMainWindow(DWORD pid){
    struct Found{DWORD pid;HWND window=nullptr;unsigned count=0;} found{pid};
    const auto visitor=[](HWND h,LPARAM state)->BOOL{auto* f=reinterpret_cast<Found*>(state);if(scaleMain(h,f->pid)){f->window=h;++f->count;}return TRUE;};
    return EnumWindows(visitor,reinterpret_cast<LPARAM>(&found))&&found.count==1?found.window:nullptr;
}
class NoFocusAutomation {
    template<class T>using Ptr=Microsoft::WRL::ComPtr<T>;
    bool comOwned_=false;
    Ptr<IUIAutomation> ui_;Ptr<IUIAutomation2> ui2_;
    std::vector<Ptr<IUIAutomationElement>> watched_;Ptr<IUIAutomationPropertyChangedEventHandler> handler_;
    static std::wstring name(IUIAutomationElement* element){BSTR b=nullptr;if(!element||FAILED(element->get_CurrentName(&b)))return {};std::wstring s=b?b:L"";SysFreeString(b);return s;}
    Ptr<IUIAutomationElement> control(HWND window,const wchar_t* id){
        Ptr<IUIAutomationElement> root,element;Ptr<IUIAutomationCondition> condition;
        if(FAILED(ui_->ElementFromHandle(window,&root)))return element;
        VARIANT value;VariantInit(&value);value.vt=VT_BSTR;value.bstrVal=SysAllocString(id);
        const auto hr=ui_->CreatePropertyCondition(UIA_AutomationIdPropertyId,value,&condition);VariantClear(&value);
        if(SUCCEEDED(hr))root->FindFirst(TreeScope_Descendants,condition.Get(),&element);
        return element;
    }
public:
    ~NoFocusAutomation(){unlisten();ui2_.Reset();ui_.Reset();if(comOwned_)CoUninitialize();}
    bool begin(){
        const auto init=CoInitializeEx(nullptr,COINIT_MULTITHREADED);if(FAILED(init))return false;comOwned_=true;
        return reconnect();
    }
    bool reconnect(){
        unlisten();ui2_.Reset();ui_.Reset();if(!comOwned_)return false;
        if(FAILED(CoCreateInstance(CLSID_CUIAutomation8,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&ui_)))||FAILED(ui_.As(&ui2_)))return false;
        if(FAILED(ui2_->put_ConnectionTimeout(600))||FAILED(ui2_->put_TransactionTimeout(600)))return false;
        BOOL focus=TRUE;
        return SUCCEEDED(ui2_->put_AutoSetFocus(FALSE))&&SUCCEEDED(ui2_->get_AutoSetFocus(&focus))&&!focus;
    }
    bool listen(HWND window,IUIAutomationPropertyChangedEventHandler* handler){
        unlisten();handler_=handler;PROPERTYID property=UIA_NamePropertyId;unsigned subscribed=0;
        for(const auto* id:{L"ScaleButton",L"StatusString"}){
            auto element=control(window,id);
            if(element&&SUCCEEDED(ui_->AddPropertyChangedEventHandlerNativeArray(element.Get(),TreeScope_Element,nullptr,handler,&property,1))){watched_.push_back(element);++subscribed;}
        }
        return subscribed==2;
    }
    void unlisten(){if(ui_&&handler_)for(const auto& element:watched_)ui_->RemovePropertyChangedEventHandler(element.Get(),handler_.Get());watched_.clear();handler_.Reset();}
    ScaleSnapshot observe(HWND window,const std::wstring& title){
        ScaleSnapshot s;auto button=control(window,L"ScaleButton");if(!button)return s;
        s.button=scaleTrim(name(button.Get()));s.state=scaleButtonState(s.button);s.readable=!s.button.empty();
        s.status=name(control(window,L"StatusString").Get());s.profileMatches=scaleProfileMatches(s.status,title);s.paused=scalePaused(s.status);return s;
    }
    ScaleInvoke invoke(HWND window,ScaleButton expected){
        auto button=control(window,L"ScaleButton");if(!button||scaleButtonState(name(button.Get()))!=expected)return {};
        Ptr<IUIAutomationInvokePattern> pattern;
        if(FAILED(button->GetCurrentPatternAs(UIA_InvokePatternId,IID_PPV_ARGS(&pattern))))return {};
        BOOL focus=TRUE;if(FAILED(ui2_->get_AutoSetFocus(&focus))||focus)return {};
        return {true,SUCCEEDED(pattern->Invoke())};
    }
};
// Shared predicate is consumed by the real foreground adapter and fake-game tests.
inline bool scaleGameMatches(const Identity& expected,const Identity& actual){
    const bool pathOnly=expected.pid==0&&expected.created==0;
    return actual.pid&&actual.created&&(pathOnly||(expected.pid&&expected.created&&actual.pid==expected.pid&&actual.created==expected.created))&&
        !expected.path.empty()&&_wcsicmp(expected.path.c_str(),actual.path.c_str())==0;
}
// Legacy helper retained for its existing regression cases; normal ScalePorts
// uses GameWindowRedrawPorts below, with no fallback to this activation cycle.
// A one-pixel, alpha-blended native tool window supplies a real activation
// transition like briefly opening YMCC. No game placement/display mode changes.
class GameWindowRefreshPorts {
    HWND target_,helper_=nullptr;DWORD pid_;ScaleProcess game_;std::function<bool()> ready_;bool opened_;
    static ATOM windowClass(){
        static const ATOM atom=[]{WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;
            cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YMCC.Lossless.WindowRefresh.1";
            return RegisterClassW(&cls);}();return atom;
    }
public:
    GameWindowRefreshPorts(HWND target,const Identity& game,std::function<bool()> ready)
        :target_(target),pid_(game.pid),ready_(std::move(ready)),opened_(game_.open(game,false)){}
    ~GameWindowRefreshPorts(){if(helper_)DestroyWindow(helper_);}
    std::uintptr_t target()const{return reinterpret_cast<std::uintptr_t>(target_);}
    std::uintptr_t helper()const{return reinterpret_cast<std::uintptr_t>(helper_);}
    std::uint64_t now()const{return GetTickCount64();}
    void pause()const{
        MSG message{};for(unsigned count=0;count<16&&helper_&&PeekMessageW(&message,helper_,0,0,PM_REMOVE);++count){
            TranslateMessage(&message);DispatchMessageW(&message);
        }Sleep(20);
    }
    WindowRefreshState observe(){
        DWORD pid=0;const bool window=IsWindow(target_)&&GetWindowThreadProcessId(target_,&pid)&&pid==pid_&&!IsIconic(target_);
        LASTINPUTINFO input{sizeof(input)};const bool known=GetLastInputInfo(&input)!=FALSE;
        return {opened_&&game_.alive()&&window,ready_(),known,
            reinterpret_cast<std::uintptr_t>(GetForegroundWindow()),input.dwTime};
    }
    bool create(){
        if(!windowClass())return false;RECT bounds{};if(!GetWindowRect(target_,&bounds))return false;
        helper_=CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_LAYERED|WS_EX_TRANSPARENT,
            MAKEINTRESOURCEW(windowClass()),L"",WS_POPUP,bounds.left+1,bounds.top+1,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        if(!helper_)return false;
        if(!SetLayeredWindowAttributes(helper_,0,1,LWA_ALPHA)){DestroyWindow(helper_);helper_=nullptr;return false;}
        ShowWindow(helper_,SW_SHOWNOACTIVATE);return true;
    }
    bool activateHelper(){return helper_&&SetForegroundWindow(helper_)!=FALSE;}
    bool activateGame(){return SetForegroundWindow(target_)!=FALSE;}
    bool destroy(){if(!helper_)return true;const bool removed=DestroyWindow(helper_)!=FALSE;
        if(removed||!IsWindow(helper_)){helper_=nullptr;return true;}return false;}
};
// RDW_UPDATENOW/RDW_ERASENOW are deliberately absent: no synchronous game
// painting or display reset. Methods1/2 are retained as regression controls;
// normal method3 requests top/foreground once, without a helper or key input.
class GameWindowRedrawPorts {
public:
    using WindowRequest=std::function<BOOL(HWND)>;
private:
    HWND target_;DWORD pid_;ScaleProcess game_;std::function<bool()> ready_;bool opened_;
    WindowRequest bring_,foreground_;
public:
    static constexpr UINT flags=RDW_INVALIDATE|RDW_FRAME|RDW_ALLCHILDREN;
    static constexpr UINT frameFlags=SWP_ASYNCWINDOWPOS|SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED;
    GameWindowRedrawPorts(HWND target,const Identity& game,std::function<bool()> ready,
                          WindowRequest bring=::BringWindowToTop,WindowRequest foreground=::SetForegroundWindow)
        :target_(target),pid_(game.pid),ready_(std::move(ready)),opened_(game_.open(game,false)),bring_(std::move(bring)),foreground_(std::move(foreground)){}
    std::uintptr_t target()const{return reinterpret_cast<std::uintptr_t>(target_);}
    std::uint64_t now()const{return GetTickCount64();}
    WindowRefreshState observe(){
        DWORD pid=0;const bool window=IsWindow(target_)&&GetWindowThreadProcessId(target_,&pid)&&pid==pid_&&!IsIconic(target_);
        LASTINPUTINFO input{sizeof(input)};const bool known=GetLastInputInfo(&input)!=FALSE;
        return {opened_&&game_.alive()&&window,ready_(),known,
            reinterpret_cast<std::uintptr_t>(GetForegroundWindow()),input.dwTime};
    }
    bool bringToTop(){return bring_(target_)!=FALSE;}
    bool requestForeground(){return foreground_(target_)!=FALSE;}
    bool frameChanged(){return SetWindowPos(target_,nullptr,0,0,0,0,frameFlags)!=FALSE;}
    bool redraw(){return RedrawWindow(target_,nullptr,nullptr,flags)!=FALSE;}
};
struct ScaleRequest{Identity ls,game;std::wstring profileTitle;bool reapply=false,refreshWindow=false,resetOnFailure=false;};
class ScalePorts {
    ScaleRequest request_;std::function<bool()> ready_;ScaleProcess process_,gameProcess_;NoFocusAutomation automation_;
    HANDLE mutex_=nullptr;bool locked_=false,opened_=false;std::uint64_t started_=GetTickCount64();
    HWND target_=nullptr;DWORD targetPid_=0;std::uint64_t targetCreated_=0;bool reconnected_=false;
public:
    ScalePorts(ScaleRequest request,std::function<bool()> ready):request_(std::move(request)),ready_(std::move(ready)){}
    ~ScalePorts(){unlock();if(mutex_)CloseHandle(mutex_);}
    bool ready()const{return ready_()&&GetTickCount64()-started_<12000;}
    bool valid(){if(opened_)return process_.alive()&&(!request_.game.pid||gameProcess_.alive());opened_=process_.open(request_.ls)&&(!request_.game.pid||gameProcess_.open(request_.game,false));return opened_;}
    bool lock(){
        DWORD session=0;if(!ProcessIdToSessionId(GetCurrentProcessId(),&session))return false;
        auto key=L"Local\\YMCC.LosslessScale."+std::to_wstring(session)+L"."+std::to_wstring(request_.ls.pid)+L"."+std::to_wstring(request_.ls.created);
        mutex_=CreateMutexW(nullptr,FALSE,key.c_str());if(!mutex_)return false;
        auto wait=WaitForSingleObject(mutex_,0);locked_=wait==WAIT_OBJECT_0||wait==WAIT_ABANDONED;return locked_;
    }
    void unlock(){if(locked_){ReleaseMutex(mutex_);locked_=false;}}
    bool begin(){return scaleAbsolute(request_.game.path)&&!request_.profileTitle.empty()&&automation_.begin();}
    std::uint64_t now()const{return GetTickCount64();}
    std::uint64_t windowBudget()const{return 4500;}
    std::uint64_t totalBudget()const{return 11000;}
    void pause()const{Sleep(100);}
    bool foregroundMatches(){
        const auto h=GetForegroundWindow();if(!h||(target_&&h!=target_))return false;
        DWORD pid=0;GetWindowThreadProcessId(h,&pid);if(!pid||(request_.game.pid&&pid!=request_.game.pid)||(targetPid_&&pid!=targetPid_))return false;
        HANDLE p=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,pid);if(!p)return false;
        wchar_t image[32768];DWORD size=32768;FILETIME c{},e{},k{},u{};
        const bool ok=WaitForSingleObject(p,0)==WAIT_TIMEOUT&&QueryFullProcessImageNameW(p,0,image,&size)&&GetProcessTimes(p,&c,&e,&k,&u)&&
            _wcsicmp(std::wstring(image,size).c_str(),request_.game.path.c_str())==0;
        CloseHandle(p);if(!ok)return false;
        const auto created=(std::uint64_t(c.dwHighDateTime)<<32)|c.dwLowDateTime;
        if(!scaleGameMatches(request_.game,Identity{pid,created,std::wstring(image,size)})||(targetCreated_&&created!=targetCreated_))return false;
        target_=h;targetPid_=pid;targetCreated_=created;return true;
    }
    bool reapply()const{return request_.reapply;}
    bool refreshRequested()const{return request_.refreshWindow;}
    // resetOnFailure is accepted only for old IPC callers; returned states never kill.
    WindowRefreshResult refreshOnce(){
        if(!ready()||!valid()||!foregroundMatches()){
            WindowRefreshResult r;r.reason="window-refresh-target-changed";return r;
        }
        GameWindowRedrawPorts ports(target_,Identity{targetPid_,targetCreated_,request_.game.path},[this]{return ready()&&valid();});
        return requestGameForegroundOnce(ports);
    }
    ScaleSnapshot observe(){
        if(!process_.alive())return {};const auto h=scaleMainWindow(request_.ls.pid);
        auto result=h?automation_.observe(h,request_.profileTitle):ScaleSnapshot{};
        // A shell/desktop transition may invalidate the UIA client/provider.
        // Reconnect once inside this action, without showing or focusing LS.
        if(!result.readable&&!reconnected_&&GetTickCount64()-started_>=1200){
            reconnected_=true;if(automation_.reconnect()&&h)result=automation_.observe(h,request_.profileTitle);
        }
        return result;
    }
    ScaleInvoke stopOnce(){
        if(!ready()||!valid()||!foregroundMatches())return {};
        const auto h=scaleMainWindow(request_.ls.pid);if(!h)return {};
        const auto current=automation_.observe(h,request_.profileTitle);
        if(!current.readable||current.state!=ScaleButton::On||!current.profileMatches)return {};
        return automation_.invoke(h,ScaleButton::On);
    }
    ScaleInvoke invokeOnce(){
        if(!ready()||!valid()||!foregroundMatches())return {};
        const auto h=scaleMainWindow(request_.ls.pid);if(!h)return {};
        return automation_.invoke(h,ScaleButton::Off);
    }
    bool cancelPending(){
        if(!ready()||!valid())return false;const auto h=scaleMainWindow(request_.ls.pid);
        return h&&automation_.invoke(h,ScaleButton::Countdown).accepted;
    }
};
inline ScaleSnapshot observeScaling(const Identity& ls,bool& available){
    ScaleProcess process;NoFocusAutomation automation;available=false;
    if(!process.open(ls)||!automation.begin())return {};
    const auto h=scaleMainWindow(ls.pid);if(!h)return {};
    const auto snapshot=automation.observe(h,L"");available=process.alive()&&snapshot.readable;return snapshot;
}
} // namespace ymcc::lossless
