#pragma once
// One original WebView/controller and DOM; this owns only its DirectComposition
// visual and ordinary desktop input forwarding. No input acquisition or timers.
#include <windows.h>
#include <windowsx.h>
#include <array>
#include <dcomp.h>
#include <wrl.h>
#include <WebView2.h>
#pragma comment(lib,"dcomp.lib")
namespace ymcc::webview {
class CompositionHost {
    template<class T> using Ptr=Microsoft::WRL::ComPtr<T>;
    HWND window_=nullptr;
    Ptr<ICoreWebView2CompositionController> composition_;
    Ptr<ICoreWebView2Controller> controller_;
    Ptr<ICoreWebView2Environment3> environment_;
    Ptr<IDCompositionDevice> device_;
    Ptr<IDCompositionTarget> target_;
    Ptr<IDCompositionVisual> visual_;
    EventRegistrationToken cursorToken_{};
    bool cursorRegistered_=false,tracking_=false;
    unsigned buttons_=0;
    bool backgroundInput_=false;
    HWND backgroundSource_=nullptr;
    POINT lastMouse_{};
    struct TouchLease{UINT id=0;Ptr<ICoreWebView2PointerInfo> info;};
    std::array<TouchLease,8> touchLeases_{};
    HRESULT lastError_=S_OK;
    const char* failureStage_="none";
    static bool promotedMouse() noexcept {
        return (GetMessageExtraInfo()&0xffffff00)==0xff515700;
    }
    bool mouse(UINT message,WPARAM w,LPARAM l,LRESULT& result) {
        if(promotedMouse()&&message!=WM_MOUSELEAVE)return false;
        COREWEBVIEW2_MOUSE_EVENT_KIND kind{};bool known=true;unsigned bit=0;bool down=false,up=false;
        switch(message) {
        case WM_MOUSEMOVE:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE;break;
        case WM_MOUSELEAVE:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_LEAVE;tracking_=false;break;
        case WM_LBUTTONDOWN:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOWN;bit=1;down=true;break;
        case WM_LBUTTONUP:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP;bit=1;up=true;break;
        case WM_LBUTTONDBLCLK:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_DOUBLE_CLICK;bit=1;down=true;break;
        case WM_RBUTTONDOWN:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOWN;bit=2;down=true;break;
        case WM_RBUTTONUP:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP;bit=2;up=true;break;
        case WM_RBUTTONDBLCLK:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_DOUBLE_CLICK;bit=2;down=true;break;
        case WM_MBUTTONDOWN:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOWN;bit=4;down=true;break;
        case WM_MBUTTONUP:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP;bit=4;up=true;break;
        case WM_MBUTTONDBLCLK:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_DOUBLE_CLICK;bit=4;down=true;break;
        case WM_XBUTTONDOWN:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_DOWN;bit=GET_XBUTTON_WPARAM(w)==XBUTTON1?8:16;down=true;break;
        case WM_XBUTTONUP:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP;bit=GET_XBUTTON_WPARAM(w)==XBUTTON1?8:16;up=true;break;
        case WM_XBUTTONDBLCLK:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_DOUBLE_CLICK;bit=GET_XBUTTON_WPARAM(w)==XBUTTON1?8:16;down=true;break;
        case WM_MOUSEWHEEL:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_WHEEL;break;
        case WM_MOUSEHWHEEL:kind=COREWEBVIEW2_MOUSE_EVENT_KIND_HORIZONTAL_WHEEL;break;
        default:known=false;break;
        }
        if(!known)return false;
        POINT point{GET_X_LPARAM(l),GET_Y_LPARAM(l)};
        const HWND receiver=backgroundInput_&&backgroundSource_?backgroundSource_:window_;
        if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL)ScreenToClient(window_,&point);
        else if(backgroundInput_&&backgroundSource_&&message!=WM_MOUSELEAVE)MapWindowPoints(receiver,window_,&point,1);
        if(message==WM_MOUSELEAVE){if(backgroundInput_)releaseMouse();point={};}
        if(backgroundInput_&&down){RECT client{};if(!GetClientRect(window_,&client)||!PtInRect(&client,point)){result=0;return true;}}
        if(message!=WM_MOUSELEAVE)lastMouse_=point;
        if(down){buttons_|=bit;if(!backgroundInput_){SetCapture(window_);controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);}}
        if(message==WM_MOUSEMOVE&&!tracking_){TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,receiver,0};tracking_=TrackMouseEvent(&track)!=FALSE;}
        const auto keys=static_cast<COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS>(GET_KEYSTATE_WPARAM(w));
        UINT32 data=0;
        if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL)data=static_cast<UINT32>(static_cast<INT32>(GET_WHEEL_DELTA_WPARAM(w)));
        if(message==WM_XBUTTONDOWN||message==WM_XBUTTONUP||message==WM_XBUTTONDBLCLK)data=GET_XBUTTON_WPARAM(w);
        lastError_=composition_->SendMouseInput(kind,keys,data,point);
        if(up){buttons_&=~bit;if(!buttons_&&GetCapture()==window_)ReleaseCapture();}
        result=message==WM_XBUTTONDOWN||message==WM_XBUTTONUP||message==WM_XBUTTONDBLCLK?TRUE:0;
        return true;
    }
    bool pointer(UINT message,WPARAM w,LRESULT& result) {
        if(message!=WM_POINTERDOWN&&message!=WM_POINTERUPDATE&&message!=WM_POINTERUP&&
           message!=WM_POINTERENTER&&message!=WM_POINTERLEAVE&&message!=WM_POINTERCAPTURECHANGED)return false;
        const UINT id=GET_POINTERID_WPARAM(w);
        if(backgroundInput_&&message==WM_POINTERCAPTURECHANGED){releasePointer(id);result=0;return true;}
        POINTER_INFO source{};
        if(!GetPointerInfo(id,&source)||(source.pointerType!=PT_TOUCH&&source.pointerType!=PT_PEN)){if(backgroundInput_)releasePointer(id);return false;}
        Ptr<ICoreWebView2PointerInfo> info;
        if(FAILED(lastError_=environment_->CreateCoreWebView2PointerInfo(&info))){if(backgroundInput_)releasePointer(id);result=0;return true;}
        POINT point=source.ptPixelLocation,raw=source.ptPixelLocationRaw;ScreenToClient(window_,&point);ScreenToClient(window_,&raw);
        RECT device{},display{};GetPointerDeviceRects(source.sourceDevice,&device,&display);
        info->put_PointerKind(source.pointerType);info->put_PointerId(source.pointerId);info->put_FrameId(source.frameId);
        info->put_PointerFlags(source.pointerFlags);info->put_PointerDeviceRect(device);info->put_DisplayRect(display);
        info->put_PixelLocation(point);info->put_PixelLocationRaw(raw);info->put_HimetricLocation(source.ptHimetricLocation);
        info->put_HimetricLocationRaw(source.ptHimetricLocationRaw);info->put_Time(source.dwTime);info->put_HistoryCount(source.historyCount);
        info->put_InputData(source.InputData);info->put_KeyStates(source.dwKeyStates);info->put_PerformanceCount(source.PerformanceCount);
        info->put_ButtonChangeKind(source.ButtonChangeType);
        if(source.pointerType==PT_TOUCH){POINTER_TOUCH_INFO touch{};if(!GetPointerTouchInfo(id,&touch)){if(backgroundInput_)releasePointer(id);result=0;return true;}
            RECT contact=touch.rcContact,contactRaw=touch.rcContactRaw;
            MapWindowPoints(HWND_DESKTOP,window_,reinterpret_cast<POINT*>(&contact),2);MapWindowPoints(HWND_DESKTOP,window_,reinterpret_cast<POINT*>(&contactRaw),2);
            info->put_TouchFlags(touch.touchFlags);info->put_TouchMask(touch.touchMask);info->put_TouchContact(contact);info->put_TouchContactRaw(contactRaw);
            info->put_TouchOrientation(touch.orientation);info->put_TouchPressure(touch.pressure);
        }else{POINTER_PEN_INFO pen{};if(!GetPointerPenInfo(id,&pen)){if(backgroundInput_)releasePointer(id);result=0;return true;}
            info->put_PenFlags(pen.penFlags);info->put_PenMask(pen.penMask);info->put_PenPressure(pen.pressure);info->put_PenRotation(pen.rotation);
            info->put_PenTiltX(pen.tiltX);info->put_PenTiltY(pen.tiltY);
        }
        return dispatchPointer(message,id,source.pointerFlags,info,result);
    }
    bool dispatchPointer(UINT message,UINT id,POINTER_FLAGS flags,const Ptr<ICoreWebView2PointerInfo>& info,LRESULT& result) {
        if(message==WM_POINTERDOWN&&!backgroundInput_)controller_->MoveFocus(COREWEBVIEW2_MOVE_FOCUS_REASON_PROGRAMMATIC);
        TouchLease* lease=nullptr;
        if(backgroundInput_){
            for(auto& item:touchLeases_)if(item.id==id){lease=&item;break;}
            if(!lease&&message==WM_POINTERDOWN)for(auto& item:touchLeases_)if(!item.id){lease=&item;item.id=id;break;}
            if(!lease&&message!=WM_POINTERENTER&&message!=WM_POINTERLEAVE){result=0;return true;}
            if(lease)lease->info=info;
            if(flags&POINTER_FLAG_CANCELED){releasePointer(id);result=0;return true;}
        }
        lastError_=composition_->SendPointerInput(static_cast<COREWEBVIEW2_POINTER_EVENT_KIND>(message),info.Get());
        if(FAILED(lastError_)&&backgroundInput_){releasePointer(id);result=0;return true;}
        if(lease&&message==WM_POINTERUP){lease->id=0;lease->info.Reset();}
        result=0;return true;
    }
    void releaseMouse() noexcept {
        if(!composition_){buttons_=0;return;}
        const unsigned held=buttons_;buttons_=0;
        // Cancellation must not turn an abandoned press into a click on its
        // original control. Move outside the client before releasing buttons.
        const POINT outside{-1,-1};
        if(held)composition_->SendMouseInput(COREWEBVIEW2_MOUSE_EVENT_KIND_MOVE,COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,0,outside);
        const auto send=[&](unsigned bit,COREWEBVIEW2_MOUSE_EVENT_KIND kind,UINT32 data=0){if(held&bit)composition_->SendMouseInput(kind,COREWEBVIEW2_MOUSE_EVENT_VIRTUAL_KEYS_NONE,data,outside);};
        send(1,COREWEBVIEW2_MOUSE_EVENT_KIND_LEFT_BUTTON_UP);send(2,COREWEBVIEW2_MOUSE_EVENT_KIND_RIGHT_BUTTON_UP);
        send(4,COREWEBVIEW2_MOUSE_EVENT_KIND_MIDDLE_BUTTON_UP);send(8,COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP,XBUTTON1);send(16,COREWEBVIEW2_MOUSE_EVENT_KIND_X_BUTTON_UP,XBUTTON2);
    }
    void releasePointer(UINT id) noexcept {
        for(auto& lease:touchLeases_)if(lease.id==id){
            auto info=lease.info;lease.id=0;lease.info.Reset();
            if(info&&composition_){
                // This WebView API has no CANCEL event kind; CANCELED on UP
                // alone is still delivered as pointerup. Move the abandoned
                // contact outside first so it cannot become a tap/click.
                const POINT outside{-32,-32};const RECT contact{-37,-37,-27,-27};
                info->put_PixelLocation(outside);info->put_PixelLocationRaw(outside);
                info->put_TouchContact(contact);info->put_TouchContactRaw(contact);
                info->put_PointerFlags(POINTER_FLAG_UPDATE|POINTER_FLAG_INRANGE|POINTER_FLAG_INCONTACT);
                composition_->SendPointerInput(COREWEBVIEW2_POINTER_EVENT_KIND_UPDATE,info.Get());
                info->put_PointerFlags(POINTER_FLAG_UP|POINTER_FLAG_CANCELED);
                composition_->SendPointerInput(COREWEBVIEW2_POINTER_EVENT_KIND_UP,info.Get());
            }
            return;
        }
    }
public:
    void cancelBackgroundInput() noexcept {
        if(!backgroundInput_)return;
        releaseMouse();for(auto& lease:touchLeases_)if(lease.id)releasePointer(lease.id);
        const HWND receiver=backgroundSource_?backgroundSource_:window_;
        if(receiver&&tracking_){TRACKMOUSEEVENT track{sizeof(track),TME_CANCEL|TME_LEAVE,receiver,0};TrackMouseEvent(&track);tracking_=false;}
    }
    void setBackgroundInput(bool enabled) noexcept {cancelBackgroundInput();backgroundInput_=enabled;if(!enabled)backgroundSource_=nullptr;}
    bool setBackgroundInputSource(HWND source) noexcept {
        if(source){DWORD pid=0;const DWORD thread=GetWindowThreadProcessId(source,&pid);
            if(!backgroundInput_||pid!=GetCurrentProcessId()||thread!=GetCurrentThreadId())return false;}
        cancelBackgroundInput();backgroundSource_=source;return true;
    }
    ~CompositionHost(){reset();}
    bool active()const noexcept{return composition_!=nullptr;}
    HRESULT error()const noexcept{return lastError_;}
    const char* failureStage()const noexcept{return failureStage_;}
    HRESULT prepare(HWND window,ICoreWebView2Environment* environment) {
        reset();if(!window||!environment)return lastError_=E_INVALIDARG;
        window_=window;failureStage_="window-creation-style";
        HRESULT hr=(GetWindowLongPtrW(window,GWL_EXSTYLE)&WS_EX_NOREDIRECTIONBITMAP)?S_OK:E_INVALIDARG;
        if(SUCCEEDED(hr)){failureStage_="environment-query";hr=environment->QueryInterface(IID_PPV_ARGS(&environment_));}
        if(SUCCEEDED(hr)){failureStage_="composition-device";hr=DCompositionCreateDevice(nullptr,IID_PPV_ARGS(&device_));}
        if(SUCCEEDED(hr)){failureStage_="composition-target";hr=device_->CreateTargetForHwnd(window,TRUE,&target_);}
        if(SUCCEEDED(hr)){failureStage_="composition-visual";hr=device_->CreateVisual(&visual_);}
        if(SUCCEEDED(hr)){failureStage_="target-root";hr=target_->SetRoot(visual_.Get());}
        if(SUCCEEDED(hr)){failureStage_="composition-commit";hr=device_->Commit();}
        if(FAILED(hr))reset();else failureStage_="prepared";return lastError_=hr;
    }
    HRESULT attach(HWND window,ICoreWebView2Environment* environment,ICoreWebView2CompositionController* composition) {
        if(!window||!environment||!composition)return lastError_=E_INVALIDARG;
        HRESULT hr=S_OK;
        if(window_!=window||!visual_||!target_||!device_)hr=prepare(window,environment);
        if(SUCCEEDED(hr)){
            composition_=composition;failureStage_="controller-query";hr=composition->QueryInterface(IID_PPV_ARGS(&controller_));
        }
        if(SUCCEEDED(hr)){failureStage_="webview-visual-root";hr=composition_->put_RootVisualTarget(visual_.Get());}
        if(SUCCEEDED(hr)){failureStage_="composition-commit";hr=device_->Commit();}
        if(SUCCEEDED(hr)){
            failureStage_="cursor-observer";hr=composition_->add_CursorChanged(Microsoft::WRL::Callback<ICoreWebView2CursorChangedEventHandler>(
                [this](ICoreWebView2CompositionController*,IUnknown*)->HRESULT{if(window_&&GetCapture()==window_)setCursor();return S_OK;}).Get(),&cursorToken_);
            cursorRegistered_=SUCCEEDED(hr);
        }
        if(FAILED(hr))reset();else failureStage_="none";return lastError_=hr;
    }
    // Move the existing controller and visual together; never create a second
    // WebView/DOM or activate either parent. The old target stays alive until
    // the move commits so a failed move can restore the original binding.
    HRESULT retarget(HWND destination) {
        if(!active()||!controller_||!device_||!visual_||!target_||!destination||!IsWindow(destination))return E_INVALIDARG;
        if(destination==window_)return S_OK;
        cancelBackgroundInput();
        if(!(GetWindowLongPtrW(destination,GWL_EXSTYLE)&WS_EX_NOREDIRECTIONBITMAP))return E_INVALIDARG;
        Ptr<IDCompositionTarget> next;
        HRESULT hr=device_->CreateTargetForHwnd(destination,TRUE,&next);
        if(FAILED(hr))return lastError_=hr;
        const HWND previousWindow=window_;
        const auto previousTarget=target_;
        hr=controller_->put_ParentWindow(destination);
        if(SUCCEEDED(hr))hr=previousTarget->SetRoot(nullptr);
        if(SUCCEEDED(hr))hr=next->SetRoot(visual_.Get());
        if(SUCCEEDED(hr))hr=device_->Commit();
        if(SUCCEEDED(hr)){target_=next;window_=destination;failureStage_="none";return lastError_=S_OK;}
        next->SetRoot(nullptr);
        const HRESULT parentRestored=controller_->put_ParentWindow(previousWindow);
        const HRESULT rootRestored=previousTarget->SetRoot(visual_.Get());
        const HRESULT committed=device_->Commit();
        if(FAILED(parentRestored)||FAILED(rootRestored)||FAILED(committed)){
            controller_->put_IsVisible(FALSE);failureStage_="retarget-rollback-failed";
        }else failureStage_="retarget-failed-original-restored";
        return lastError_=hr;
    }
    void setCursor(){HCURSOR cursor=nullptr;if(composition_&&SUCCEEDED(composition_->get_Cursor(&cursor))&&cursor)SetCursor(cursor);}
    void reset() noexcept {
        cancelBackgroundInput();backgroundInput_=false;backgroundSource_=nullptr;
        if(window_&&GetCapture()==window_)ReleaseCapture();
        if(window_&&tracking_){TRACKMOUSEEVENT track{sizeof(track),TME_CANCEL|TME_LEAVE,window_,0};TrackMouseEvent(&track);}
        if(composition_){if(cursorRegistered_)composition_->remove_CursorChanged(cursorToken_);composition_->put_RootVisualTarget(nullptr);}
        if(target_)target_->SetRoot(nullptr);if(device_)device_->Commit();
        composition_.Reset();controller_.Reset();environment_.Reset();visual_.Reset();target_.Reset();device_.Reset();window_=nullptr;
        buttons_=0;tracking_=false;cursorRegistered_=false;
    }
    bool handle(HWND window,UINT message,WPARAM w,LPARAM l,LRESULT& result) {
        const HWND receiver=backgroundInput_&&backgroundSource_?backgroundSource_:window_;
        if(!active()||window!=receiver)return false;
        // Only these original input branches consume a message. Other UI messages
        // need no window-state queries and leave this input owner untouched.
        switch(message){
        case WM_SETCURSOR:
        case WM_CANCELMODE:
        case WM_CAPTURECHANGED:
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
        case WM_POINTERENTER:
        case WM_POINTERLEAVE:
        case WM_POINTERCAPTURECHANGED:
        case WM_MOUSEMOVE:
        case WM_MOUSELEAVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_LBUTTONDBLCLK:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_RBUTTONDBLCLK:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_MBUTTONDBLCLK:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP:
        case WM_XBUTTONDBLCLK:
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL:
            break;
        default:return false;
        }
        if(!IsWindowEnabled(receiver)||!IsWindowVisible(receiver)||!IsWindowVisible(window_))return false;
        if(message==WM_SETCURSOR&&LOWORD(l)==HTCLIENT){setCursor();result=TRUE;return true;}
        if(message==WM_CANCELMODE||message==WM_CAPTURECHANGED){if(backgroundInput_)cancelBackgroundInput();else buttons_=0;return false;}
        if(pointer(message,w,result))return true;
        return mouse(message,w,l,result);
    }
};
}
