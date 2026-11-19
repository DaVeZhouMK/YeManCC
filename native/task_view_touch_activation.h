#pragma once
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <tuple>

// An explicit, single contact for the already-visible main WebView. This does
// not identify a DOM node or prove delivery of its trusted touch/click event.
namespace ymcc::taskViewTouch {
// Keep the application's existing NTDDI target. The optional RS5 device is an
// opaque handle; all three functions are resolved together at runtime.
using Device=HANDLE;
inline constexpr ULONGLONG maxAgeMs=1000;
struct Request { ULONGLONG issued=0; double x=0,y=0,width=0,height=0; bool monotonic=false; ULONGLONG budgetMs=maxAgeMs; };
struct Result { bool downAccepted=false,upAccepted=false; const char* reason="invalid-request"; DWORD error=ERROR_INVALID_PARAMETER; const char* refusalStage=nullptr; };
struct Target { POINT point{}; RECT bounds{}; };
inline bool same(const Target& a,const Target& b) noexcept {
    return a.point.x==b.point.x&&a.point.y==b.point.y&&a.bounds.left==b.bounds.left&&
        a.bounds.top==b.bounds.top&&a.bounds.right==b.bounds.right&&a.bounds.bottom==b.bounds.bottom;
}
inline bool geometryValid(const Request& r) noexcept {
    return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&
        r.width>0&&r.height>0&&r.x>=0&&r.y>=0&&r.x<r.width&&r.y<r.height;
}
inline bool fresh(const Request& r,ULONGLONG now) noexcept {return r.issued&&r.issued<=now&&now-r.issued<=r.budgetMs;}

template<class Current,class Bounds> struct WindowsOps {
    HWND window,receiver; Current& current; Bounds& webBounds;
    mutable const char* refusalStage=nullptr;
    mutable DWORD refusalError=ERROR_CANCELLED;
    using Create=Device(WINAPI*)(POINTER_INPUT_TYPE,ULONG,POINTER_FEEDBACK_MODE);
    using Inject=BOOL(WINAPI*)(Device,const POINTER_TYPE_INFO*,UINT32);
    using Destroy=void(WINAPI*)(Device);
    Create createApi=nullptr; Inject injectApi=nullptr; Destroy destroyApi=nullptr;
    WindowsOps(HWND display,HWND input,Current& context,Bounds& bounds):window(display),receiver(input),current(context),webBounds(bounds) {
        const auto user=GetModuleHandleW(L"user32.dll");
        if(user){
            createApi=reinterpret_cast<Create>(GetProcAddress(user,"CreateSyntheticPointerDevice"));
            injectApi=reinterpret_cast<Inject>(GetProcAddress(user,"InjectSyntheticPointerInput"));
            destroyApi=reinterpret_cast<Destroy>(GetProcAddress(user,"DestroySyntheticPointerDevice"));
        }
    }
    ULONGLONG unixMs() const noexcept {
        FILETIME time{};GetSystemTimeAsFileTime(&time);ULARGE_INTEGER value{};
        value.LowPart=time.dwLowDateTime;value.HighPart=time.dwHighDateTime;
        return value.QuadPart/10000ULL-11644473600000ULL;
    }
    ULONGLONG tickMs() const noexcept {return GetTickCount64();}
    bool available() const noexcept {return createApi&&injectApi&&destroyApi;}
    DWORD error() const noexcept {const auto e=GetLastError();return e?e:ERROR_GEN_FAILURE;}
    Device create() const noexcept {return createApi(PT_TOUCH,1,POINTER_FEEDBACK_NONE);}
    bool inject(Device d,const POINTER_TYPE_INFO& f) const noexcept {return injectApi(d,&f,1)!=FALSE;}
    void destroy(Device d) const noexcept {destroyApi(d);}
    bool pause() const noexcept {
        LARGE_INTEGER frequency{},before{},after{};
        if(!QueryPerformanceFrequency(&frequency)||frequency.QuadPart<=0||!QueryPerformanceCounter(&before))return false;
        Sleep(1);
        // A scheduler yield alone is not proof that the .1ms interval elapsed.
        return QueryPerformanceCounter(&after)&&after.QuadPart>=before.QuadPart&&
            (static_cast<long double>(after.QuadPart)-before.QuadPart)*10000>=frequency.QuadPart;
    }
    bool desktopCurrent() const noexcept {
        const HDESK desktop=OpenInputDesktop(0,FALSE,DESKTOP_READOBJECTS);
        if(!desktop)return false;
        struct Close {HDESK value;~Close(){CloseDesktop(value);}} close{desktop};
        wchar_t inputName[64]{},threadName[64]{},stationName[64]{};DWORD needed=0;BOOL receiving=FALSE;
        return GetUserObjectInformationW(desktop,UOI_NAME,inputName,sizeof(inputName),&needed)&&
            GetUserObjectInformationW(GetThreadDesktop(GetCurrentThreadId()),UOI_NAME,threadName,sizeof(threadName),&needed)&&
            GetUserObjectInformationW(GetProcessWindowStation(),UOI_NAME,stationName,sizeof(stationName),&needed)&&
            _wcsicmp(inputName,L"Default")==0&&_wcsicmp(threadName,inputName)==0&&_wcsicmp(stationName,L"WinSta0")==0&&
            GetUserObjectInformationW(desktop,UOI_IO,&receiving,sizeof(receiving),&needed)&&receiving;
    }
    const char* refusalReason() const noexcept {return refusalStage?refusalStage:"context-unavailable";}
    DWORD refusalCode() const noexcept {return refusalError;}
    bool refuse(const char* stage,DWORD error=ERROR_CANCELLED) const noexcept {
        refusalStage=stage;refusalError=error;return false;
    }
    bool ownedRoot(HWND candidate,bool enabled) const noexcept {
        DWORD process=0;
        return candidate&&IsWindow(candidate)&&IsWindowVisible(candidate)&&!IsIconic(candidate)&&
            (!enabled||IsWindowEnabled(candidate))&&GetWindowThreadProcessId(candidate,&process)==GetCurrentThreadId()&&
            process==GetCurrentProcessId()&&GetAncestor(candidate,GA_ROOT)==candidate;
    }
    bool locate(const Request& r,Target& target) const {
        refusalStage=nullptr;refusalError=ERROR_CANCELLED;
        if(const auto stage=current())return refuse(stage);
        // The overlay visual display is deliberately WS_DISABLED. Only its
        // separate actual input receiver must be enabled.
        if(!ownedRoot(window,false))return refuse("display-unavailable");
        if(!ownedRoot(receiver,true))return refuse("receiver-unavailable");
        if(!desktopCurrent())return refuse("desktop-unavailable");
        if(const auto stage=webBounds(target.bounds))return refuse(stage);
        const auto& b=target.bounds;
        const auto width=std::int64_t(b.right)-b.left,height=std::int64_t(b.bottom)-b.top;
        if(width<=0||height<=0)return refuse("geometry-unavailable",ERROR_INVALID_PARAMETER);
        const auto x=static_cast<long double>(b.left)+std::floor(static_cast<long double>(r.x)/r.width*width);
        const auto y=static_cast<long double>(b.top)+std::floor(static_cast<long double>(r.y)/r.height*height);
        if(x<b.left||x>=b.right||y<b.top||y>=b.bottom||
            x<(std::numeric_limits<LONG>::min)()||x>(std::numeric_limits<LONG>::max)()||
            y<(std::numeric_limits<LONG>::min)()||y>(std::numeric_limits<LONG>::max)())return refuse("geometry-unavailable",ERROR_INVALID_PARAMETER);
        target.point={static_cast<LONG>(x),static_cast<LONG>(y)};
        RECT client{};
        if(!GetClientRect(window,&client)||!PtInRect(&client,target.point)||!ClientToScreen(window,&target.point))
            return refuse("geometry-unavailable",ERROR_INVALID_PARAMETER);
        POINT inputPoint=target.point;RECT inputClient{};
        if(!GetClientRect(receiver,&inputClient)||!ScreenToClient(receiver,&inputPoint)||!PtInRect(&inputClient,inputPoint))
            return refuse("receiver-point-outside",ERROR_INVALID_PARAMETER);
        const HWND hit=WindowFromPoint(target.point);
        if(!hit||GetAncestor(hit,GA_ROOT)!=receiver)return refuse("target-occluded");
        if(const auto stage=current())return refuse(stage);
        return true;
    }
};

// The .1ms/ERROR_NOT_READY same-frame rule is documented for InjectTouchInput:
// https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-injecttouchinput
// Synthetic-pointer docs do not separately promise that timing rule. We only
// retry that explicit error once, after 1ms, with the identical frame and the
// OS-assigned timestamp. A successful DOWN is never submitted a second time.
template<class Ops,class Validate> bool sendFrame(Ops& ops,Device device,
        const POINTER_TYPE_INFO& frame,Validate&& validate,DWORD& error) {
    if(!validate()){error=ERROR_CANCELLED;return false;}
    if(ops.inject(device,frame)){error=ERROR_SUCCESS;return true;}
    error=ops.error();
    if(error!=ERROR_NOT_READY)return false;
    if(!ops.pause())return false;
    if(!validate()){error=ERROR_CANCELLED;return false;}
    if(ops.inject(device,frame)){error=ERROR_SUCCESS;return true;}
    error=ops.error();return false;
}
template<class Ops> struct Contact {
    Ops& ops; Device device; POINTER_TYPE_INFO frame{};bool active=false;
    bool cancel(DWORD& error) {
        if(!active)return true;
        active=false;frame.touchInfo.pointerInfo.pointerFlags=POINTER_FLAG_CANCELED|POINTER_FLAG_UP;
        return sendFrame(ops,device,frame,[]{return true;},error);
    }
    ~Contact(){if(active){DWORD ignored=0;cancel(ignored);}ops.destroy(device);}
};
template<class Ops> Result activate(const Request& request,Ops& ops) {
    Result result;
    if(!geometryValid(request))return result;
    const auto now=[&]{return request.monotonic?ops.tickMs():ops.unixMs();};
    if(!fresh(request,now())){result.reason="expired-or-future-request";result.error=ERROR_TIMEOUT;return result;}
    Target target{};
    if(!ops.locate(request,target)){result.reason=ops.refusalReason();result.refusalStage=result.reason;result.error=ops.refusalCode();return result;}
    if(!ops.available()){result.reason="api-unavailable";result.error=ERROR_PROC_NOT_FOUND;return result;}
    const auto device=ops.create();
    if(!device){result.reason="device-create-failed";result.error=ops.error();return result;}
    Contact<Ops> contact{ops,device};
    contact.frame.type=PT_TOUCH;
    auto& pointer=contact.frame.touchInfo.pointerInfo;
    pointer.pointerType=PT_TOUCH;pointer.pointerId=1;pointer.ptPixelLocation=target.point;
    pointer.pointerFlags=POINTER_FLAG_DOWN|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT;
    const auto current=[&]{
        if(!fresh(request,now())){result.refusalStage="request-expired";return false;}
        Target next{};
        if(!ops.locate(request,next)){result.refusalStage=ops.refusalReason();return false;}
        if(!same(target,next)){result.refusalStage="geometry-changed";return false;}
        return true;
    };
    if(!sendFrame(ops,device,contact.frame,current,result.error)){result.reason="down-not-accepted";return result;}
    result.downAccepted=true;contact.active=true;
    pointer.pointerFlags=POINTER_FLAG_UP;
    if(!sendFrame(ops,device,contact.frame,current,result.error)){
        const bool lost=result.error==ERROR_CANCELLED;DWORD cleanup=0;
        const bool canceled=contact.cancel(cleanup);
        result.reason=lost?(canceled?"owner-lost-after-down":"owner-lost-after-down-cancel-unconfirmed"):
            (canceled?"up-not-accepted":"up-not-accepted-cancel-unconfirmed");
        return result;
    }
    contact.active=false;result.upAccepted=true;result.reason="accepted";result.error=ERROR_SUCCESS;return result;
}
}
