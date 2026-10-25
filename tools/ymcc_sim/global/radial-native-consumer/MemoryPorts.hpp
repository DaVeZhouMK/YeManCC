#pragma once
#include "json.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
using Json=nlohmann::json;
using UINT=unsigned;using WORD=unsigned short;using BYTE=unsigned char;using BOOL=int;using LONG=long;using LONG_PTR=std::intptr_t;using WPARAM=std::uintptr_t;using LPARAM=std::intptr_t;using LRESULT=std::intptr_t;using HRESULT=long;using ULONGLONG=unsigned long long;
struct WindowTag{};struct RegionTag{};using HWND=WindowTag*;using HRGN=RegionTag*;using LPWSTR=wchar_t*;using LPCWSTR=const wchar_t*;
struct POINT{LONG x,y;};struct RECT{LONG left=0,top=0,right=0,bottom=0;};struct CREATESTRUCTW{void* lpCreateParams;};struct PAINTSTRUCT{};
#define CALLBACK
constexpr HRESULT S_OK=0,E_FAIL=-1;inline bool FAILED(HRESULT h){return h<0;}
constexpr BOOL TRUE=1,FALSE=0;constexpr UINT WM_APP=0x8000,WM_TIMER=0x113,WM_POWERBROADCAST=0x218,PBT_APMSUSPEND=4,WM_CLOSE=0x10,WM_DISPLAYCHANGE=0x7e,WM_DPICHANGED=0x2e0,WM_NCHITTEST=0x84,WM_POINTERACTIVATE=0x24b,WM_MOUSEACTIVATE=0x21,WM_ERASEBKGND=0x14,WM_PAINT=0xf,WM_NCCREATE=0x81;
constexpr int SW_SHOWNOACTIVATE=4,SW_HIDE=0,GWLP_USERDATA=-21,HTTRANSPARENT=-1,PA_NOACTIVATE=3,MA_NOACTIVATE=3,RGN_OR=2,RGN_DIFF=4,WINDING=2;
namespace memory {
struct Window{LONG_PTR user=0;HRGN region=nullptr;};struct Message{HWND window;UINT message;WPARAM w;LPARAM l;};
inline std::map<HWND,Window> windows;inline std::set<HRGN> regions;inline std::set<void*> wireAllocations;inline std::vector<Message> messages;inline Json trace=Json::array();inline std::uintptr_t next=10;inline ULONGLONG now=1000;
inline auto number(const void*p){return reinterpret_cast<std::uintptr_t>(p);}inline void log(const char*op,Json data=Json::object()){data["op"]=op;trace.push_back(std::move(data));}
inline HWND window(LONG_PTR user=0){auto p=reinterpret_cast<HWND>(++next);windows[p]={user,nullptr};log("window-port-created",{{"handle",number(p)}});return p;}
inline Json resources(){return {{"windows",windows.size()},{"regions",regions.size()},{"wireAllocations",wireAllocations.size()},{"queuedMessages",messages.size()}};}
inline void begin(){if(!windows.empty()||!regions.empty()||!wireAllocations.empty()||!messages.empty())throw std::runtime_error("PRIOR_MEMORY_ENDPOINT_NOT_RETURNED");trace=Json::array();now=1000;}
inline LPWSTR allocate(const std::wstring&s){auto p=static_cast<LPWSTR>(std::malloc((s.size()+1)*sizeof(wchar_t)));if(!p)throw std::bad_alloc();std::memcpy(p,s.c_str(),(s.size()+1)*sizeof(wchar_t));wireAllocations.insert(p);return p;}
inline std::wstring wideAscii(const std::string&s){std::wstring r;for(unsigned char c:s){if(c>127)throw std::runtime_error("ASCII_ONLY_MEMORY_TRANSPORT_DOMAIN");r.push_back(static_cast<wchar_t>(c));}return r;}
inline std::string narrowAscii(const std::wstring&s){std::string r;for(wchar_t c:s){if(c<0||c>127)throw std::runtime_error("ASCII_ONLY_MEMORY_TRANSPORT_DOMAIN");r.push_back(static_cast<char>(c));}return r;}
}
inline void CoTaskMemFree(void*p){memory::wireAllocations.erase(p);std::free(p);}
inline HRGN CreateRectRgn(int a,int b,int c,int d){auto p=reinterpret_cast<HRGN>(++memory::next);memory::regions.insert(p);memory::log("CreateRectRgn",{{"handle",memory::number(p)},{"rect",{a,b,c,d}}});return p;}
inline HRGN CreatePolygonRgn(const POINT*p,int count,int fill){Json points=Json::array();for(int i=0;i<count;i++)points.push_back({p[i].x,p[i].y});auto h=reinterpret_cast<HRGN>(++memory::next);memory::regions.insert(h);memory::log("CreatePolygonRgn",{{"handle",memory::number(h)},{"points",points},{"fill",fill}});return h;}
inline HRGN CreateEllipticRgn(int a,int b,int c,int d){auto p=reinterpret_cast<HRGN>(++memory::next);memory::regions.insert(p);memory::log("CreateEllipticRgn",{{"handle",memory::number(p)},{"rect",{a,b,c,d}}});return p;}
inline int CombineRgn(HRGN target,HRGN a,HRGN b,int mode){memory::log("CombineRgn",{{"target",memory::number(target)},{"a",memory::number(a)},{"b",memory::number(b)},{"mode",mode}});return 1;}
inline BOOL DeleteObject(HRGN h){memory::log("DeleteObject",{{"handle",memory::number(h)}});return memory::regions.erase(h)?TRUE:FALSE;}
inline BOOL SetWindowRgn(HWND w,HRGN h,BOOL redraw){auto at=memory::windows.find(w);memory::log("SetWindowRgn",{{"window",memory::number(w)},{"region",memory::number(h)},{"redraw",redraw}});if(at==memory::windows.end()||!memory::regions.count(h))return FALSE;if(at->second.region)DeleteObject(at->second.region);at->second.region=h;return TRUE;}
inline BOOL ShowWindow(HWND h,int mode){memory::log("ShowWindow",{{"window",memory::number(h)},{"mode",mode}});return memory::windows.count(h)?TRUE:FALSE;}
inline BOOL DestroyWindow(HWND h){auto at=memory::windows.find(h);memory::log("DestroyWindow",{{"window",memory::number(h)}});if(at==memory::windows.end())return FALSE;if(at->second.region)DeleteObject(at->second.region);memory::windows.erase(at);return TRUE;}
inline BOOL KillTimer(HWND h,UINT id){memory::log("KillTimer",{{"window",memory::number(h)},{"id",id}});return TRUE;}
inline BOOL PostMessageW(HWND h,UINT m,WPARAM w,LPARAM l){memory::log("PostMessageW",{{"window",memory::number(h)},{"message",m}});memory::messages.push_back({h,m,w,l});return TRUE;}
inline LONG_PTR GetWindowLongPtrW(HWND h,int){auto at=memory::windows.find(h);return at==memory::windows.end()?0:at->second.user;}
inline LONG_PTR SetWindowLongPtrW(HWND h,int,LONG_PTR p){auto at=memory::windows.find(h);if(at==memory::windows.end())return 0;auto old=at->second.user;at->second.user=p;return old;}
inline ULONGLONG GetTickCount64(){return memory::now;}
inline LRESULT DefWindowProcW(HWND,UINT,WPARAM,LPARAM){return 0;}
inline int BeginPaint(HWND,PAINTSTRUCT*){return 1;}inline BOOL EndPaint(HWND,PAINTSTRUCT*){return TRUE;}
struct ICoreWebView2WebMessageReceivedEventArgs{virtual HRESULT get_Source(LPWSTR*)=0;virtual HRESULT get_WebMessageAsJson(LPWSTR*)=0;virtual~ICoreWebView2WebMessageReceivedEventArgs()=default;};
struct ICoreWebView2{virtual HRESULT PostWebMessageAsJson(LPCWSTR)=0;virtual~ICoreWebView2()=default;};
struct ICoreWebView2Controller{virtual HRESULT Close()=0;virtual~ICoreWebView2Controller()=default;};
struct ICoreWebView2Environment{};struct ICoreWebView2CompositionController{};struct IDCompositionDevice{};struct IDCompositionTarget{};struct IDCompositionVisual{};
namespace Microsoft::WRL {
// Transport/resource ports only: no native policy or COM ABI emulation is claimed.
template<class T>class ComPtr{T*p=nullptr;public:ComPtr()=default;ComPtr(T*v):p(v){};ComPtr&operator=(T*v){p=v;return *this;}T*Get()const{return p;}T*operator->()const{return p;}explicit operator bool()const{return p!=nullptr;}void Reset(){memory::log("ComPtr.Reset",{{"nonNull",p!=nullptr}});p=nullptr;}};
template<class T,class F>T*Callback(F&&);
}
namespace ymcc::desktophud {inline std::wstring wide(const std::string&s){return memory::wideAscii(s);}inline std::string utf8(const std::wstring&s){return memory::narrowAscii(s);}}
namespace ymcc::screeninput {
struct Runtime{};struct Surface {bool contacts=false,syncResult=true;unsigned cancelCalls=0,closeCalls=0,syncCalls=0;bool hasContacts()const{return contacts;}bool sync(){++syncCalls;memory::log("Surface.sync");return syncResult;}void cancel(){++cancelCalls;contacts=false;memory::log("Surface.cancel");}void close(){++closeCalls;memory::log("Surface.close");}};
}
