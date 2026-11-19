#pragma once
// Borrow the monitor's exact bounded per-pixel HWND receiver. It has no
// WebView child, composition input site, keyboard owner, capture or worker.
#include "monitor_hud_panel_input.h"
#include "monitor_hud_input_policy.h"
#include "webview_composition_host.h"
namespace ymcc::gameoverlayinput {
class PanelReceiver {
    HWND display_=nullptr,window_=nullptr;
    webview::CompositionHost* host_=nullptr;
    hudinput::PanelInput input_;
    bool closing_=false,retired_=false,placing_=false;
    RECT bounds_{};
    unsigned activationWithdrawals_=0;
    static bool same(const RECT& a,const RECT& b){return EqualRect(&a,&b)!=FALSE;}
    static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l){
        auto* self=reinterpret_cast<PanelReceiver*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){self=static_cast<PanelReceiver*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(self));}
        if(m==WM_NCDESTROY)SetWindowLongPtrW(h,GWLP_USERDATA,0);
        if(self&&!self->closing_){
            if(m==WM_DESTROY&&self->window_==h){self->retired_=true;self->window_=nullptr;self->withdraw();return 0;}
            if(m==WM_SETFOCUS||(m==WM_ACTIVATE&&LOWORD(w)!=WA_INACTIVE)){
                ++self->activationWithdrawals_;self->retired_=true;self->withdraw();return 0;
            }
            if(m==WM_CLOSE){self->retired_=true;self->withdraw();return 0;}
            if(m==WM_WINDOWPOSCHANGING&&l&&self->window_==h){
                auto* p=reinterpret_cast<WINDOWPOS*>(l);
                if(!(p->flags&SWP_NOMOVE)){p->x=self->bounds_.left;p->y=self->bounds_.top;}
                if(!(p->flags&SWP_NOSIZE)){p->cx=self->bounds_.right-self->bounds_.left;p->cy=self->bounds_.bottom-self->bounds_.top;}
                p->flags|=SWP_NOACTIVATE;
            }
            if(m==WM_WINDOWPOSCHANGED&&!self->placing_&&self->window_==h){
                RECT actual{};if(!GetWindowRect(h,&actual)||!same(actual,self->bounds_)){self->retired_=true;self->withdraw();return 0;}
            }
            if((m==WM_CANCELMODE||(m==WM_SHOWWINDOW&&!w))&&self->host_)self->host_->cancelBackgroundInput();
            if(m==WM_TOUCHHITTESTING){
                const auto* input=reinterpret_cast<const TOUCH_HIT_TESTING_INPUT*>(l);
                if(!input||!self->ready()||!PtInRect(&self->bounds_,input->point))return hudinput::rejectTouchHitTest(l);
            }
            if(self->host_&&self->window_==h){LRESULT result=0;if(self->host_->handle(h,m,w,l,result))return result;}
        }
        if(m==WM_MOUSEACTIVATE)return MA_NOACTIVATE;
        if(m==WM_POINTERACTIVATE)return PA_NOACTIVATE;
        if(m==WM_NCHITTEST)return HTCLIENT;
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_PAINT){PAINTSTRUCT p;BeginPaint(h,&p);EndPaint(h,&p);return 0;}
        return DefWindowProcW(h,m,w,l);
    }
    void withdraw() noexcept {
        if(closing_)return;closing_=true;
        if(host_)host_->setBackgroundInputSource(nullptr);
        input_.quiesce();input_.close();
        const HWND h=window_;window_=nullptr;
        if(h&&IsWindow(h)){SetWindowLongPtrW(h,GWLP_USERDATA,0);DestroyWindow(h);}
        closing_=false;
    }
public:
    ~PanelReceiver(){close();}
    PanelReceiver()=default;
    PanelReceiver(const PanelReceiver&)=delete;PanelReceiver& operator=(const PanelReceiver&)=delete;
    bool ready() const {return !retired_&&window_&&IsWindowVisible(window_)&&input_.active();}
    HWND window() const {return window_;}
    bool retired() const {return retired_;}
    unsigned activationWithdrawals()const{return activationWithdrawals_;}
    bool terminalReleased()const{return !window_&&input_.terminalReleased();}
    bool attach(HWND display,webview::CompositionHost& host){
        close();retired_=false;display_=display;host_=&host;activationWithdrawals_=0;
        return synchronize();
    }
    bool synchronize(){
        if(retired_)return true; // Keep display/controller alive; no focus-repair loop.
        if(!display_||!IsWindow(display_)||!host_)return false;
        RECT client{};POINT origin{};
        if(!GetClientRect(display_,&client)||!ClientToScreen(display_,&origin)||client.right<=0||client.bottom<=0)return false;
        bounds_={origin.x,origin.y,origin.x+client.right,origin.y+client.bottom};
        if(!window_){
            WNDCLASSW c{};c.lpfnWndProc=procedure;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"YMCC_OverlayMonitorPanelInput";
            if(!RegisterClassW(&c)&&GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return false;
            window_=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_NOREDIRECTIONBITMAP,c.lpszClassName,L"",WS_POPUP,
                bounds_.left,bounds_.top,client.right,client.bottom,nullptr,nullptr,c.hInstance,this);
            if(!window_||!input_.attach(window_)||!host_->setBackgroundInputSource(window_)){withdraw();return false;}
        }
        if(!IsWindowVisible(display_))return true;
        struct Placement{bool& value;~Placement(){value=false;}} guard{placing_};placing_=true;
        const HWND placed=window_;
        if(!SetWindowPos(placed,HWND_TOPMOST,bounds_.left,bounds_.top,client.right,client.bottom,SWP_NOACTIVATE|SWP_SHOWWINDOW)||window_!=placed)return false;
        if(!input_.ensureMousePass())return false;
        RECT actual{};return GetWindowRect(placed,&actual)&&same(actual,bounds_);
    }
    void close() noexcept {withdraw();host_=nullptr;display_=nullptr;retired_=false;bounds_={};}
};
}
