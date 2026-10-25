#pragma once
#include <windows.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
namespace ymcc::radial {
/** Passive compatibility source only. Windows marks promoted touch mouse packets
 * with MI_WP_SIGNATURE and bit 7. Apps which suppress promotion do not expose a
 * contact through this channel; this is not a global WM_POINTER registration. */
class PromotedTouchHold {
 bool down_=false;POINT point_{};ULONGLONG deadline_=0;
public:
 static bool touch(ULONG_PTR extra){return (extra&0xffffff00ULL)==0xff515700ULL&&(extra&0x80ULL)!=0;}
 bool active()const{return down_;}POINT point()const{return point_;}ULONGLONG deadline()const{return deadline_;}
 void cancel(){down_=false;deadline_=0;}
 void observe(WPARAM message,ULONG_PTR extra,POINT point,ULONGLONG now,unsigned duration){
  if(!touch(extra)){if(message!=WM_MOUSEMOVE)cancel();return;}
  if(message==WM_LBUTTONDOWN){down_=true;point_=point;deadline_=now+std::clamp(duration,400u,1500u);return;}
  if(message==WM_LBUTTONUP||message==WM_RBUTTONDOWN||message==WM_RBUTTONUP){cancel();return;}
  if(message==WM_MOUSEMOVE&&down_&&std::hypot(double(point.x-point_.x),double(point.y-point_.y))>=8)cancel();
 }
 bool take(ULONGLONG now){if(!down_||now<deadline_)return false;if(now-deadline_>2000){cancel();return false;}cancel();return true;}
};
class PromotedTouchObserver {
 static inline PromotedTouchObserver* owner=nullptr;
 static constexpr UINT_PTR timerId=0x5953;
 HHOOK hook=nullptr;HWND anchor=nullptr,foreground=nullptr;unsigned duration=650;PromotedTouchHold hold;
 std::function<void(int,int)> summon;std::function<bool(POINT)> permits;
 static LRESULT CALLBACK observe(int code,WPARAM message,LPARAM data){
  auto* self=owner;if(code>=0&&self&&data){const auto& packet=*reinterpret_cast<const MSLLHOOKSTRUCT*>(data);
   if(!self->permits||self->permits(packet.pt)){
    self->hold.observe(message,packet.dwExtraInfo,packet.pt,GetTickCount64(),self->duration);
    if(self->hold.active()&&message==WM_LBUTTONDOWN){self->foreground=GetForegroundWindow();SetTimer(self->anchor,timerId,self->duration,timer);}
    if(!self->hold.active())KillTimer(self->anchor,timerId);
   }else{self->hold.cancel();KillTimer(self->anchor,timerId);}
  }
  // Never consume, synthesize or replay another application's touch/mouse input.
  return CallNextHookEx(self?self->hook:nullptr,code,message,data);
 }
 static void CALLBACK timer(HWND,UINT,UINT_PTR,DWORD){auto* self=owner;if(!self)return;KillTimer(self->anchor,timerId);const auto p=self->hold.point();
  if(self->foreground!=GetForegroundWindow()||(self->permits&&!self->permits(p))){self->hold.cancel();return;}
  if(self->hold.take(GetTickCount64())&&self->summon)self->summon(p.x,p.y);
 }
public:
 bool configure(HWND window,bool enabled,unsigned ms,std::function<void(int,int)> action,std::function<bool(POINT)> allowed){
  shutdown();anchor=window;duration=std::clamp(ms,400u,1500u);summon=std::move(action);permits=std::move(allowed);
  if(!enabled)return true;owner=this;hook=SetWindowsHookExW(WH_MOUSE_LL,observe,GetModuleHandleW(nullptr),0);if(!hook)owner=nullptr;return hook!=nullptr;
 }
 bool installed()const{return hook!=nullptr;}
 void cancelContact(){hold.cancel();if(anchor)KillTimer(anchor,timerId);foreground=nullptr;}
 void shutdown(){hold.cancel();if(anchor)KillTimer(anchor,timerId);if(hook)UnhookWindowsHookEx(hook);hook=nullptr;foreground=nullptr;if(owner==this)owner=nullptr;}
 ~PromotedTouchObserver(){shutdown();}
};
}
