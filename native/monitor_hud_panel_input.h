#pragma once
// Monitor-only bounded HWND input. No Composition input site, dispatcher, hook,
// thread, timer or explicit pointer/mouse capture. Touchpad surfaces keep their
// existing independent input implementation.
#include <windows.h>
#include <algorithm>
namespace ymcc::hudinput {
class PanelInput {
 HWND root_=nullptr;DWORD thread_=0;LONG_PTR original_=0;SIZE uploaded_{};bool quiet_=false;
 bool upload(SIZE size) noexcept {
   const HDC screen=GetDC(nullptr);if(!screen)return false;
   const HDC memory=CreateCompatibleDC(screen);BITMAPINFO info{};
   info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=size.cx;info.bmiHeader.biHeight=-size.cy;
   info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;void* pixels=nullptr;
   const HBITMAP bitmap=memory?CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0):nullptr;bool ok=false;
   if(bitmap&&pixels){const auto previous=SelectObject(memory,bitmap);std::fill_n(static_cast<DWORD*>(pixels),(size_t)size.cx*size.cy,0x01000000u);
     RECT rect{};GetWindowRect(root_,&rect);POINT origin{},position{rect.left,rect.top};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};
     ok=UpdateLayeredWindow(root_,screen,&position,&size,memory,&origin,0,&blend,ULW_ALPHA)!=FALSE;SelectObject(memory,previous);
   }
   if(bitmap)DeleteObject(bitmap);if(memory)DeleteDC(memory);ReleaseDC(nullptr,screen);
   if(ok){uploaded_=size;++uploads;}return ok;
 }
public:
 HRESULT error=S_OK;UINT64 uploads=0,cacheHits=0;
 PanelInput()=default;PanelInput(const PanelInput&)=delete;PanelInput& operator=(const PanelInput&)=delete;
 bool usesWindowMouse()const noexcept{return true;}
 bool controlsMouse()const noexcept{return false;}
 bool active()const noexcept{return !quiet_&&root_&&IsWindow(root_)&&IsWindowEnabled(root_)&&uploaded_.cx>0&&uploaded_.cy>0&&
   (GetWindowLongPtrW(root_,GWL_EXSTYLE)&(WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT))==(WS_EX_LAYERED|WS_EX_NOACTIVATE);}
 bool terminalReleased()const noexcept{return !root_;}
 bool ensureMousePass(bool force=false) noexcept {
   if(quiet_||!root_||thread_!=GetCurrentThreadId()){error=E_ABORT;return false;}
   const auto style=GetWindowLongPtrW(root_,GWL_EXSTYLE);
   constexpr LONG_PTR mask=WS_EX_LAYERED|WS_EX_NOACTIVATE|WS_EX_TOOLWINDOW;
   BYTE alpha=0;COLORREF key=0;DWORD flags=0;
   if((style&(mask|WS_EX_TRANSPARENT))!=mask||GetLayeredWindowAttributes(root_,&key,&alpha,&flags)){
     // Drop constant-alpha attributes before returning to per-pixel alpha.
     SetLastError(0);SetWindowLongPtrW(root_,GWL_EXSTYLE,style&~WS_EX_LAYERED);if(GetLastError()){error=HRESULT_FROM_WIN32(GetLastError());return false;}
     SetLastError(0);SetWindowLongPtrW(root_,GWL_EXSTYLE,(style|mask)&~WS_EX_TRANSPARENT);if(GetLastError()){error=HRESULT_FROM_WIN32(GetLastError());return false;}
     uploaded_={};SetWindowPos(root_,nullptr,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOZORDER|SWP_NOACTIVATE|SWP_FRAMECHANGED);
   }
   if(!IsWindowEnabled(root_))EnableWindow(root_,TRUE);
   RECT rect{};if(!GetClientRect(root_,&rect)){error=HRESULT_FROM_WIN32(GetLastError());return false;}
   const SIZE size{rect.right,rect.bottom};if(size.cx<=0||size.cy<=0||size.cx>16384||size.cy>16384){error=E_INVALIDARG;return false;}
   if(force||size.cx!=uploaded_.cx||size.cy!=uploaded_.cy){if(!upload(size)){error=E_FAIL;return false;}}else ++cacheHits;
   error=active()?S_OK:E_FAIL;return SUCCEEDED(error);
 }
 bool attach(HWND root) noexcept {
   DWORD pid=0;const DWORD thread=GetWindowThreadProcessId(root,&pid);
   if(!root||pid!=GetCurrentProcessId()||thread!=GetCurrentThreadId()){error=RPC_E_WRONG_THREAD;return false;}
   if(root_==root)return ensureMousePass();close();root_=root;thread_=thread;original_=GetWindowLongPtrW(root,GWL_EXSTYLE);quiet_=false;
   if(!RegisterTouchHitTestingWindow(root_,TOUCH_HIT_TESTING_CLIENT)){const DWORD last=GetLastError();const HRESULT failed=last?HRESULT_FROM_WIN32(last):E_FAIL;close();error=failed;return false;}
   return ensureMousePass();
 }
 void cancel() noexcept {
   if(!root_||thread_!=GetCurrentThreadId())return;
   TRACKMOUSEEVENT tracking{sizeof(tracking),TME_CANCEL|TME_LEAVE,root_,0};TrackMouseEvent(&tracking);
   // Migration/foreign code may have captured our HWND. Never release another
   // owner's capture; this implementation never acquires capture itself.
   if(GetCapture()==root_)ReleaseCapture();
 }
 bool quiesce() noexcept {
   if(root_&&thread_!=GetCurrentThreadId()){error=RPC_E_WRONG_THREAD;return false;}
   quiet_=true;if(root_&&IsWindow(root_)){ShowWindow(root_,SW_HIDE);EnableWindow(root_,FALSE);}cancel();
   return !root_||!IsWindow(root_)||(!IsWindowVisible(root_)&&!IsWindowEnabled(root_));
 }
 void close() noexcept {
   if(root_&&thread_!=GetCurrentThreadId()){error=RPC_E_WRONG_THREAD;return;}cancel();
   // The carrier destroys the HWND immediately afterwards. Do not restore an
   // enabled hit-test surface during reentrant close/destruction callbacks.
   if(root_&&IsWindow(root_)){ShowWindow(root_,SW_HIDE);EnableWindow(root_,FALSE);}
   root_=nullptr;thread_=0;original_=0;uploaded_={};quiet_=false;
 }
};
}
