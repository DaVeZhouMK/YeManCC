#pragma once
// Owned HWND/Composition bridge. Geometry/sinks remain on the UI thread.
// Final recovery may use a touchpad-owned event-driven input apartment.
// Touch/Pen stay controlled; ordinary mouse hit-tests the lower window unless
// the owning HUD explicitly opts into mouse gestures while unlocked.
// Historical candidate build/switch names are retained for compatibility.
#include <windows.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Numerics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Core.h>
#include <winrt/Windows.UI.Input.h>
#include <winrt/Windows.Devices.Input.h>
#include <windows.ui.composition.interop.h>
#include <DispatcherQueue.h>
#pragma comment(lib,"CoreMessaging.lib")
#pragma comment(lib,"WindowsApp.lib")
#include "screen_touchpad_input_channel.h"
namespace ymcc::screeninput {
using Compositor=winrt::Windows::UI::Composition::Compositor;

struct Runtime {
    DWORD thread=0;HRESULT error=S_OK;
    bool isolatedInput=false;std::shared_ptr<InputChannel> inputChannel;
    bool retireInputChannel() noexcept {if(inputChannel&&!inputChannel->shutdown()){error=HRESULT_FROM_WIN32(ERROR_TIMEOUT);return false;}inputChannel.reset();return true;}
    bool useFreshInputChannel() noexcept {if(!retireInputChannel())return false;isolatedInput=true;error=S_OK;return true;}
    winrt::Windows::System::DispatcherQueueController queue{nullptr};Compositor compositor{nullptr};
    bool ensure() noexcept {
        if(thread&&thread!=GetCurrentThreadId()){error=RPC_E_WRONG_THREAD;return false;}
        if(compositor&&!isolatedInput)return true;
        APTTYPE type{};APTTYPEQUALIFIER qualifier{};
        if(FAILED(CoGetApartmentType(&type,&qualifier))||(type!=APTTYPE_STA&&type!=APTTYPE_MAINSTA)){error=CO_E_NOTINITIALIZED;return false;}
        if(isolatedInput){thread=GetCurrentThreadId();error=S_OK;return true;}
        try {
            if(!winrt::Windows::System::DispatcherQueue::GetForCurrentThread()) {
                DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_STA};
                winrt::check_hresult(CreateDispatcherQueueController(options,(ABI::Windows::System::IDispatcherQueueController**)winrt::put_abi(queue)));
            }
            compositor=Compositor();thread=GetCurrentThreadId();error=S_OK;return true;
        }catch(const winrt::hresult_error& e){error=e.code();return false;}catch(...){error=E_FAIL;return false;}
    }
};
class Surface {
    HWND root_=nullptr,child_=nullptr;DWORD thread_=0;LONG_PTR oldStyle_=0;int surface_=0;Sink sink_=nullptr;void* context_=nullptr;
    bool controlMouse_=false,windowMouse_=false;bool layered_=false,uploaded_=false,policyBusy_=false,quiesced_=false,closing_=false,terminalCloseFailed_=false,channelCloseFailed_=false;SIZE uploadedSize_{};UINT64 generation_=0;UINT_PTR cancellingInputCookie_=0;
    std::array<UINT32,16> contacts_{};std::array<bool,16> occupied_{};
    winrt::Windows::UI::Composition::Desktop::DesktopWindowTarget target_{nullptr};
    winrt::Windows::UI::Composition::SpriteVisual visual_{nullptr};
    winrt::Windows::UI::Core::CoreIndependentInputSourceController controller_{nullptr};
    winrt::Windows::UI::Core::CoreIndependentInputSource source_{nullptr};
    std::array<winrt::event_token,4> tokens_{};
    std::shared_ptr<InputChannel> inputChannel_;std::shared_ptr<InputChannel::Input> independent_;
    bool controlled()const noexcept {return independent_?independent_->ready.load()&&independent_->admitted.load()&&inputChannel_&&inputChannel_->healthy():(bool)controller_;}
    bool transparent()const {return independent_?controlled():controller_.IsTransparentForUncontrolledInput();}
    // Retain the failing operation through close/rebind. These are fixed labels,
    // observed only by the owner on its existing state/terminal receipts.
    const char* lastFailureStage_="none";
    bool fail(HRESULT code,const char* stage) noexcept {
        error=code;if(stage)lastFailureStage_=stage;return false;
    }
    static LRESULT CALLBACK childProc(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto* surface=reinterpret_cast<Surface*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){surface=static_cast<Surface*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(surface));}
        if(m==WM_NCDESTROY)SetWindowLongPtrW(h,GWLP_USERDATA,0);
        if(surface&&m==kChannelInputMessage){surface->drainIndependent((UINT_PTR)w);return 0;}
        // A monitor's legacy mouse route belongs only to its parent HWND.
        // Never forward a message to another application or synthesize input.
        if(surface&&surface->windowMouse_&&surface->root_&&
           (m==WM_LBUTTONDOWN||m==WM_LBUTTONDBLCLK||m==WM_LBUTTONUP||m==WM_MOUSEMOVE)){
            POINT point{static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};MapWindowPoints(h,surface->root_,&point,1);
            return SendMessageW(surface->root_,m,w,MAKELPARAM(point.x,point.y));
        }
        if(surface&&surface->windowMouse_&&surface->root_&&
           (m==WM_POINTERDOWN||m==WM_POINTERUPDATE||m==WM_POINTERUP||m==WM_POINTERCAPTURECHANGED))return SendMessageW(surface->root_,m,w,l);
        if(m==WM_NCHITTEST&&surface&&surface->windowMouse_)return HTTRANSPARENT;
        switch(m){case WM_MOUSEACTIVATE:return MA_NOACTIVATE;case WM_POINTERACTIVATE:return PA_NOACTIVATE;case WM_ERASEBKGND:return 1;
        case WM_PAINT:{PAINTSTRUCT ps;BeginPaint(h,&ps);EndPaint(h,&ps);return 0;}
        // Only the controlled source may publish contacts. Never double-publish
        // a fallback WM_POINTER, and never turn it into a replayed mouse input.
        case WM_POINTERDOWN:case WM_POINTERUPDATE:case WM_POINTERUP:return 0;}
        return DefWindowProcW(h,m,w,l);
    }
    int find(UINT32 id)const {for(int i=0;i<(int)contacts_.size();++i)if(occupied_[i]&&contacts_[i]==id)return i;return -1;}
    void emit(const Event& event) {if(sink_)sink_(root_,event,context_);}
    void consume(const winrt::Windows::UI::Core::PointerEventArgs& args,Phase phase,UINT64 generation) noexcept {
        // Always consume owned Touch/Pen, even after an admission/config reset.
        try {if(quiesced_||!root_)return;args.Handled(true);if(generation!=generation_||GetCurrentThreadId()!=thread_)return;
            auto point=args.CurrentPoint();const auto kind=point.PointerDevice().PointerDeviceType();
            const POINTER_INPUT_TYPE type=kind==winrt::Windows::Devices::Input::PointerDeviceType::Touch?PT_TOUCH:kind==winrt::Windows::Devices::Input::PointerDeviceType::Pen?PT_PEN:controlMouse_&&kind==winrt::Windows::Devices::Input::PointerDeviceType::Mouse?PT_MOUSE:PT_POINTER;
            if(type==PT_MOUSE&&phase==Phase::Down&&!point.Properties().IsLeftButtonPressed())return;
            if(type==PT_MOUSE&&phase==Phase::Up&&point.Properties().IsLeftButtonPressed())return;
            if(type==PT_POINTER)return;
             if(phase==Phase::Down&&!IsWindowVisible(root_))return; // A queued DOWN cannot revive a hidden owner.
             const UINT32 id=point.PointerId();int slot=find(id);
            if(phase==Phase::Down){if(slot>=0)return;for(int i=0;i<(int)occupied_.size();++i)if(!occupied_[i]){slot=i;occupied_[i]=true;contacts_[i]=id;break;}}
            if(slot<0)return;const auto p=point.Position();if(!std::isfinite(p.X)||!std::isfinite(p.Y))return;
            Event event{surface_,id,type,phase,p.X,p.Y,point.Properties().IsCanceled()||phase==Phase::Cancel};
            // Preserve the originating frame time. Queued DOWN/UP callbacks can
            // execute together even though the physical hold lasted much longer.
            // PointerPoint carries this event's boot-relative microseconds.
            // GetPointerInfo refers to the most recent Win32 pointer message,
            // which need not be this independent-source callback after a stall.
            event.sourceTime=static_cast<DWORD>(point.Timestamp()/1000);
            event.timeKnown=true;
            if(phase==Phase::Up||phase==Phase::Cancel||event.cancelled)occupied_[slot]=false;
            emit(event);
        }catch(const winrt::hresult_error& e){error=e.code();}catch(...){error=E_FAIL;}
    }
    bool uploadZero(SIZE size) noexcept {
        HDC screen=GetDC(nullptr);if(!screen)return false;HDC memory=CreateCompatibleDC(screen);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size.cx;info.bmiHeader.biHeight=-size.cy;
        info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
        HBITMAP bitmap=memory?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;bool ok=false;
        if(bitmap&&pixels){const auto previous=SelectObject(memory,bitmap);// Alpha zero is mouse-pass-through. The bounded monitor window
            // uses the lowest nonzero alpha to receive ordinary HWND mouse
            // messages while Touch/Pen stay on their independent source.
            std::fill_n((DWORD*)pixels,(size_t)size.cx*size.cy,windowMouse_?0x01000000u:0u);
            RECT rect{};GetWindowRect(root_,&rect);POINT position{rect.left,rect.top},origin{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
            ok=UpdateLayeredWindow(root_,screen,&position,&size,memory,&origin,0,&blend,ULW_ALPHA)!=FALSE;SelectObject(memory,previous);}
        if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);ReleaseDC(nullptr,screen);
        if(ok){uploaded_=true;uploadedSize_=size;++uploads;}return ok;
    }
public:
    HRESULT error=S_OK;UINT64 uploads=0,cacheHits=0;
    const char* lastFailureStage()const noexcept{return lastFailureStage_;}
    bool usesWindowMouse()const noexcept{return windowMouse_;}
    bool controlsMouse()const noexcept{return controlMouse_;}
    bool quiesced()const noexcept{return quiesced_;}
    bool hasContacts()const noexcept{return std::any_of(occupied_.begin(),occupied_.end(),[](bool occupied){return occupied;});}
    bool terminalReleased()const noexcept{return !root_&&!child_&&!source_&&!controller_&&!independent_&&!inputChannel_&&!target_&&!hasContacts()&&!terminalCloseFailed_&&!channelCloseFailed_;}
    Surface()=default;Surface(const Surface&)=delete;Surface& operator=(const Surface&)=delete;
    HWND child()const noexcept{return child_;}HWND root()const noexcept{return root_;}bool active()const noexcept{return !quiesced_&&root_&&child_&&controlled()&&layered_&&uploaded_&&
        IsWindowEnabled(root_)&&IsWindowEnabled(child_)&&
        (GetWindowLongPtrW(root_,GWL_EXSTYLE)&(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT))==(WS_EX_LAYERED|WS_EX_NOACTIVATE);}
    bool policyEnabled()const noexcept{return !quiesced_&&root_&&child_&&controlled()&&layered_&&!policyBusy_;}
    // Event-driven repair only. No input replay, source replacement, worker,
    // timer, capture reset, or general message pump in style/enable callbacks.
    bool ensureMousePass(bool force=false) noexcept {
        if(quiesced_)return fail(E_ABORT,"mouse-pass-quiesced");if(policyBusy_)return true;
        if(!root_||!child_||!controlled()||!layered_||thread_!=GetCurrentThreadId())return fail(RPC_E_WRONG_THREAD,"mouse-pass-context");
        struct Guard{bool& busy;Guard(bool& b):busy(b){busy=true;}~Guard(){busy=false;}} guard(policyBusy_);
        const char* stage="mouse-pass-style";
        try {
            auto style=GetWindowLongPtrW(root_,GWL_EXSTYLE);
            constexpr LONG_PTR required=WS_EX_LAYERED|WS_EX_NOACTIVATE;
            constexpr LONG_PTR forbidden=WS_EX_TRANSPARENT;
            BYTE alpha=0;COLORREF key=0;DWORD flags=0;
            const bool constantAlpha=GetLayeredWindowAttributes(root_,&key,&alpha,&flags)!=FALSE;
            if(constantAlpha){
                SetLastError(ERROR_SUCCESS);
                if(!SetWindowLongPtrW(root_,GWL_EXSTYLE,style&~WS_EX_LAYERED)&&GetLastError()!=ERROR_SUCCESS)winrt::throw_last_error();
                style&=~WS_EX_LAYERED;uploaded_=false;
            }
            if((style&(required|forbidden))!=required){
                SetLastError(ERROR_SUCCESS);
                if(!SetWindowLongPtrW(root_,GWL_EXSTYLE,(style|required)&~forbidden)&&GetLastError()!=ERROR_SUCCESS)winrt::throw_last_error();
                uploaded_=false;
                if(!SetWindowPos(root_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED))winrt::throw_last_error();
            }
            if(!IsWindowEnabled(root_))EnableWindow(root_,TRUE);
            if(!IsWindowEnabled(child_))EnableWindow(child_,TRUE);
            auto childStyle=GetWindowLongPtrW(child_,GWL_EXSTYLE);
            if(!(childStyle&WS_EX_NOACTIVATE)){
                SetLastError(ERROR_SUCCESS);
                if(!SetWindowLongPtrW(child_,GWL_EXSTYLE,childStyle|WS_EX_NOACTIVATE)&&GetLastError()!=ERROR_SUCCESS)winrt::throw_last_error();
            }
            stage="mouse-pass-controller";
            if(!independent_&&!controller_.IsTransparentForUncontrolledInput())controller_.IsTransparentForUncontrolledInput(true);
            stage="mouse-pass-geometry";
            RECT rect{};if(!GetClientRect(root_,&rect))winrt::throw_last_error();SIZE size{rect.right,rect.bottom};
            if(size.cx<=0||size.cy<=0||size.cx>16384||size.cy>16384)winrt::throw_hresult(E_INVALIDARG);
            if(force||!uploaded_||size.cx!=uploadedSize_.cx||size.cy!=uploadedSize_.cy){
                stage="mouse-pass-alpha-upload";
                if(!uploadZero(size)){const DWORD failure=GetLastError();winrt::throw_hresult(failure?HRESULT_FROM_WIN32(failure):E_FAIL);}
            }else ++cacheHits;
            stage="mouse-pass-verify";
            if(!active()||!transparent())winrt::throw_hresult(E_FAIL);
            error=S_OK;return true;
        }catch(const winrt::hresult_error& e){return fail(e.code(),stage);}catch(...){return fail(E_FAIL,stage);}
    }
    void drainIndependent(UINT_PTR cookie) noexcept {
        const auto input=independent_;if(!input||cookie!=input->cookie||quiesced_||closing_||thread_!=GetCurrentThreadId())return;
        std::array<Event,64> events{};bool lost=false;const auto n=input->take(events,lost);const auto generation=generation_;
        if(lost){
            // Loss cancels this gesture, not the lifetime of a healthy source.
            // Check the cancellation result before reopening its existing gate.
            HRESULT cancelled=S_OK;
            {
                struct CancelFrame{UINT_PTR& cookie;UINT_PTR previous;~CancelFrame(){cookie=previous;}};
                CancelFrame frame{cancellingInputCookie_,cancellingInputCookie_};
                cancellingInputCookie_=input->cookie;cancel(&cancelled);
            }
            if(FAILED(cancelled)){input->fence();fail(cancelled,"input-channel-overflow-cancel");return;}
            fail(HRESULT_FROM_WIN32(ERROR_BUFFER_OVERFLOW),"input-channel-overflow");
            // A cancel sink may close, replace, hide or retire this owner.
            // Its newer generation has authority; never reshow it here.
            if(!quiesced_&&!closing_&&generation_==generation+1&&independent_==input&&
               root_&&IsWindowVisible(root_)&&IsWindowEnabled(root_))sync();
            return;
        }
        for(unsigned i=0;i<n;++i){if(quiesced_||closing_||generation_!=generation||independent_!=input||!input->admitted.load())return;
            const auto event=events[i];if(event.phase==Phase::Down&&!IsWindowVisible(root_))continue;
            int slot=find(event.id);if(event.phase==Phase::Down){if(slot>=0)continue;for(int j=0;j<(int)occupied_.size();++j)if(!occupied_[j]){slot=j;occupied_[j]=true;contacts_[j]=event.id;break;}}
            if(slot<0)continue;if(event.phase==Phase::Up||event.phase==Phase::Cancel||event.cancelled)occupied_[slot]=false;
            try{emit(event);}catch(...){error=E_FAIL;}
        }
    }
    bool usesIndependentInput()const noexcept{return (bool)independent_;}
    auto source()const{return source_;}
    bool attach(Runtime& runtime,HWND root,int surface,Sink sink,void* context,bool makeTransparent=true,bool controlMouse=false,bool windowMouse=false) noexcept {
        if(closing_||quiesced_)return fail(E_ABORT,"attach-quiesced");if(root_==root&&root_)return sync();close();if(independent_)return fail(E_ABORT,"attach-channel-not-released");DWORD pid=0;const DWORD ownerThread=GetWindowThreadProcessId(root,&pid);
        if(!root||ownerThread!=GetCurrentThreadId()||pid!=GetCurrentProcessId())return fail(RPC_E_WRONG_THREAD,"attach-context");
        if(!runtime.ensure())return fail(runtime.error,"runtime-initialize");
        const auto style=GetWindowLongPtrW(root,GWL_EXSTYLE);if(style&(WS_EX_LAYERED|WS_EX_TRANSPARENT))return fail(E_INVALIDARG,"attach-root-style");
        terminalCloseFailed_=false;windowMouse_=windowMouse;root_=root;thread_=ownerThread;surface_=surface;sink_=sink;context_=context;oldStyle_=style;const auto generation=++generation_;
        const char* stage="attach-child-window";
        try {
            WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"YeManControlledTouchChild";cls.lpfnWndProc=childProc;
            if(!RegisterClassW(&cls)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)winrt::throw_last_error();
            RECT rect{};if(!GetClientRect(root,&rect))winrt::throw_last_error();
            child_=CreateWindowExW(WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,cls.lpszClassName,L"",WS_CHILD|WS_VISIBLE,0,0,rect.right,rect.bottom,root,nullptr,cls.hInstance,this);
            if(!child_)winrt::throw_last_error();
            if(runtime.isolatedInput){
                if(controlMouse||windowMouse)winrt::throw_hresult(E_INVALIDARG);
                stage="attach-owned-independent-composition";
                if(!runtime.inputChannel)runtime.inputChannel=std::make_shared<InputChannel>();inputChannel_=runtime.inputChannel;
                stage="attach-owned-channel-start";
                if(!inputChannel_->start(error))winrt::throw_hresult(FAILED(error)?error:E_FAIL);
                stage="attach-owned-command";
                if(!inputChannel_->attach(winrt::Windows::UI::Composition::Visual{nullptr},child_,surface,independent_,error,true)){
                    if(independent_&&independent_->attachCommandCompleted)stage=independent_->attachStage.load(std::memory_order_relaxed);
                    winrt::throw_hresult(FAILED(error)?error:E_FAIL);
                }
            }else{
            stage="attach-desktop-target";
            auto interop=runtime.compositor.as<ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
            winrt::check_hresult(interop->CreateDesktopWindowTarget(child_,TRUE,(ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**)winrt::put_abi(target_)));
            stage="attach-composition-visual";
            visual_=runtime.compositor.CreateSpriteVisual();visual_.Size({(float)rect.right,(float)rect.bottom});visual_.Brush(runtime.compositor.CreateColorBrush({0,0,0,0}));target_.Root(visual_);
            stage="attach-input-controller";
            controller_=winrt::Windows::UI::Core::CoreIndependentInputSourceController::CreateForVisual(visual_);
            controlMouse_=controlMouse&&!windowMouse_;
            auto controlled=winrt::Windows::UI::Core::CoreInputDeviceTypes::Touch|winrt::Windows::UI::Core::CoreInputDeviceTypes::Pen;
            if(controlMouse_)controlled=controlled|winrt::Windows::UI::Core::CoreInputDeviceTypes::Mouse;
            stage="attach-controlled-input";controller_.SetControlledInput(controlled);
            stage="attach-input-source";
            controller_.IsTransparentForUncontrolledInput(true);source_=controller_.Source();
            stage="attach-input-events";
            tokens_[0]=source_.PointerPressed([this,generation](auto const&,auto const& args){consume(args,Phase::Down,generation);});
            tokens_[1]=source_.PointerMoved([this,generation](auto const&,auto const& args){consume(args,Phase::Move,generation);});
            tokens_[2]=source_.PointerReleased([this,generation](auto const&,auto const& args){consume(args,Phase::Up,generation);});
            tokens_[3]=source_.PointerCaptureLost([this,generation](auto const&,auto const& args){consume(args,Phase::Cancel,generation);});
            }
            error=S_OK;stage=nullptr;if(makeTransparent&&!enableTransparency())winrt::throw_hresult(error);return true;
        }catch(const winrt::hresult_error& e){const auto failure=e.code();const auto failureStage=stage?stage:lastFailureStage_;close();return fail(failure,failureStage);}
        catch(...){const auto failureStage=stage?stage:lastFailureStage_;close();return fail(E_FAIL,failureStage);}
    }
    bool enableTransparency() noexcept {
        if(!root_||thread_!=GetCurrentThreadId())return fail(RPC_E_WRONG_THREAD,"transparency-context");
        if(!layered_){SetLastError(0);const auto previous=SetWindowLongPtrW(root_,GWL_EXSTYLE,oldStyle_|WS_EX_LAYERED);
            if(!previous&&GetLastError())return fail(HRESULT_FROM_WIN32(GetLastError()),"transparency-style");layered_=true;
            SetWindowPos(root_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
        return sync();
    }
    bool sync(bool force=false) noexcept {
        if(quiesced_||closing_)return fail(E_ABORT,"sync-quiesced");
        if(!root_||!child_||thread_!=GetCurrentThreadId())return fail(RPC_E_WRONG_THREAD,"sync-context");
        if(independent_&&cancellingInputCookie_==independent_->cookie)return fail(E_ABORT,"sync-cancel-pending");
        const char* stage="sync-geometry";
        try {RECT rect{};if(!GetClientRect(root_,&rect))winrt::throw_last_error();SIZE size{rect.right,rect.bottom};
            if(size.cx<=0||size.cy<=0||size.cx>16384||size.cy>16384)winrt::throw_hresult(E_INVALIDARG);
            if(!SetWindowPos(child_,nullptr,0,0,size.cx,size.cy,SWP_NOZORDER|SWP_NOACTIVATE))winrt::throw_last_error();stage="sync-composition-visual";if(visual_)visual_.Size({(float)size.cx,(float)size.cy});
            // Bind/geometry work for the independent channel is queued on its
            // dispatcher. Drain that channel before publishing alpha-zero input,
            // not a fixed Sleep and not a periodic/general Win32 message pump.
            const auto generation=generation_;const HWND root=root_;
            stage="sync-input-dispatch";
            if(independent_){stage="sync-independent-input-channel";if(!inputChannel_->sync(independent_,error,size))winrt::throw_hresult(error);}
            else{auto channel=source_;auto dispatcher=channel.Dispatcher();
            stage="sync-input-process-events";
            dispatcher.ProcessEvents(winrt::Windows::UI::Core::CoreProcessEventsOption::ProcessAllIfPresent);}
            // A queued callback may synchronously close/reconfigure its owner.
            if(generation_!=generation||root_!=root||!child_||(!visual_&&!independent_))return fail(E_ABORT,"sync-rebind-context");
            RECT current{};if(!GetClientRect(root_,&current)||current.right!=size.cx||current.bottom!=size.cy)return fail(E_ABORT,"sync-rebind-geometry");
            stage=nullptr;if(layered_&&!ensureMousePass(force))winrt::throw_hresult(error);
            error=S_OK;return true;
        }catch(const winrt::hresult_error& e){return fail(e.code(),stage);}catch(...){return fail(E_FAIL,stage);}
    }
    void cancel(HRESULT* failureResult=nullptr) noexcept {
        if(failureResult)*failureResult=S_OK;
        // A later sink may sync successfully and change error. Preserve this
        // cancellation's failures in its caller-owned result, independently.
        const auto failed=[&](HRESULT reason){error=FAILED(reason)?reason:E_FAIL;if(failureResult)*failureResult=error;};
        if(thread_&&thread_!=GetCurrentThreadId()){failed(RPC_E_WRONG_THREAD);return;}
        // Retire the entire contact set before releasing the owned input source:
        // ReleasePointerCapture can deliver CaptureLost synchronously. Clearing
        // gesture bookkeeping alone leaves that independent source capturing
        // desktop input after its HWND is hidden and later shown again.
        const auto contacts=contacts_;const auto occupied=occupied_;occupied_={};
        if(independent_){++generation_;independent_->fence();if(inputChannel_){HRESULT failure=S_OK;if(!inputChannel_->cancel(independent_,failure))failed(failure);}}
        try {if(source_&&source_.HasCapture())source_.ReleasePointerCapture();}catch(...){failed(E_FAIL);}
        const HWND captured=GetCapture();
        if(captured&&(captured==root_||captured==child_)&&!ReleaseCapture())failed(HRESULT_FROM_WIN32(GetLastError()));
        for(int i=0;i<(int)occupied.size();++i)if(occupied[i]){try{emit(Event{surface_,contacts[i],PT_TOUCH,Phase::Cancel,0,0,true});}catch(...){failed(E_FAIL);}}
    }
    // Terminal owner withdrawal only. Ordinary sync/close/rebind keeps its
    // existing display semantics; the exiting pad owner explicitly opts in.
    bool quiesce() noexcept {
        if(thread_&&thread_!=GetCurrentThreadId()){error=RPC_E_WRONG_THREAD;return false;}
        if(!quiesced_){quiesced_=true;++generation_;}if(independent_)independent_->admitted=false;policyBusy_=true;
        // No sink, WinRT, dispatcher drain or join before the HWNDs are inert.
        // The policy latch is set first so WM_ENABLE cannot re-enable them.
        if(child_&&IsWindow(child_)){ShowWindow(child_,SW_HIDE);EnableWindow(child_,FALSE);}
        if(root_&&IsWindow(root_)){ShowWindow(root_,SW_HIDE);EnableWindow(root_,FALSE);}
        const HWND captured=GetCapture();
        if(captured&&(captured==child_||captured==root_)&&!ReleaseCapture()){terminalCloseFailed_=true;error=HRESULT_FROM_WIN32(GetLastError());}
        return (!child_||!IsWindow(child_)||(!IsWindowVisible(child_)&&!IsWindowEnabled(child_)))&&
            (!root_||!IsWindow(root_)||(!IsWindowVisible(root_)&&!IsWindowEnabled(root_)));
    }
    void close() noexcept {
        if(thread_&&thread_!=GetCurrentThreadId()){error=RPC_E_WRONG_THREAD;return;}if(closing_)return;closing_=true;policyBusy_=true;
        // The UI receiver is now closed to delivery. Normal cancel/quiesce
        // does not acknowledge this; worker failure must still release contacts.
        if(independent_)independent_->consumerClosed.store(true,std::memory_order_release);
        cancel();++generation_;
        if(independent_){HRESULT failure=S_OK;if(inputChannel_&&inputChannel_->close(independent_,failure)){
            // An owning-STA input close, or the channel's complete clean
            // retirement, settles only this earlier incomplete channel call.
            // Unrelated terminal failures remain sticky.
            channelCloseFailed_=false;
            independent_.reset();inputChannel_.reset();
        }else{error=failure;channelCloseFailed_=true;}}else inputChannel_.reset();
        // Partial registration failures must not skip closing the controller/target.
        if(source_){
            try {if(tokens_[0].value)source_.PointerPressed(tokens_[0]);}catch(...){error=E_FAIL;}
            try {if(tokens_[1].value)source_.PointerMoved(tokens_[1]);}catch(...){error=E_FAIL;}
            try {if(tokens_[2].value)source_.PointerReleased(tokens_[2]);}catch(...){error=E_FAIL;}
            try {if(tokens_[3].value)source_.PointerCaptureLost(tokens_[3]);}catch(...){error=E_FAIL;}
        }
        // Capture belongs to this source, not the desktop or another overlay.
        // Event revocation and terminal generation fencing precede its release.
        try {if(quiesced_&&source_&&source_.HasCapture())source_.ReleasePointerCapture();}catch(...){error=E_FAIL;terminalCloseFailed_=true;}
        try {if(controller_)controller_.Close();}catch(...){error=E_FAIL;if(quiesced_)terminalCloseFailed_=true;}
        try {if(target_)target_.Close();}catch(...){error=E_FAIL;if(quiesced_)terminalCloseFailed_=true;}
        source_=nullptr;controller_=nullptr;visual_=nullptr;target_=nullptr;tokens_={};occupied_={};
        if(child_&&IsWindow(child_))DestroyWindow(child_);child_=nullptr;
        if(root_&&layered_&&IsWindow(root_)){SetWindowLongPtrW(root_,GWL_EXSTYLE,oldStyle_);SetWindowPos(root_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);}
        root_=nullptr;thread_=0;controlMouse_=windowMouse_=false;layered_=false;uploaded_=false;policyBusy_=false;quiesced_=false;closing_=false;uploadedSize_={};sink_=nullptr;context_=nullptr;
    }
};
} // namespace ymcc::screeninput
