#pragma once
#include <UIAutomation.h>
#include <wrl/client.h>
#include <set>
#include <mutex>
#include <atomic>
#include <functional>
namespace ymcc::losslessUiaFixture {
inline std::atomic<unsigned> initCalls{0},uninitCalls{0},factoryCalls{0},factoryFailures{0},clientsLive{0},elementsLive{0},conditionsLive{0},subscriptions{0},removed{0},processOpened{0},processClosed{0},unexpected{0};
inline std::mutex handlesMutex;
inline std::set<HANDLE> ownedProcesses;
inline std::function<void()> factoryHook;
inline std::function<void(const char*)> callHook;
inline std::atomic<unsigned> propertyNameCalls{0},findCalls{0},rootCalls{0};
inline constexpr wchar_t image[]=L"C:\\Fixture\\LosslessScaling.exe";
struct Condition final:IUIAutomationCondition {
    std::atomic<ULONG> refs{1};std::wstring id;
    explicit Condition(std::wstring value):id(std::move(value)){++conditionsLive;}
    ~Condition(){--conditionsLive;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==__uuidof(IUnknown)||iid==__uuidof(IUIAutomationCondition)){*out=static_cast<IUIAutomationCondition*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
};
struct Element final:IUIAutomationElement {
    std::atomic<ULONG> refs{1};std::wstring value;
    explicit Element(std::wstring name):value(std::move(name)){++elementsLive;}
    ~Element(){--elementsLive;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==__uuidof(IUnknown)||iid==__uuidof(IUIAutomationElement)){*out=static_cast<IUIAutomationElement*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE FindFirst(TreeScope,IUIAutomationCondition* condition,IUIAutomationElement** out)override{if(!out||!condition)return E_POINTER;++findCalls;if(callHook)callHook("find");const auto& id=static_cast<Condition*>(condition)->id;*out=new Element(id==L"ScaleButton"?L"Scale":L"Scaling: fixture\nResolution: fixture\nProfile: \"fixture\"");return S_OK;}
    HRESULT STDMETHODCALLTYPE get_CurrentName(BSTR* out)override{if(!out)return E_POINTER;++propertyNameCalls;if(callHook)callHook("name");*out=SysAllocString(value.c_str());return *out?S_OK:E_OUTOFMEMORY;}
#include "lossless_uia_element_stubs.inc"
};
struct Client final:IUIAutomation2 {
    std::atomic<ULONG> refs{1};BOOL focus=TRUE;DWORD connection=0,transaction=0;
    struct Listener{Microsoft::WRL::ComPtr<IUIAutomationElement> element;Microsoft::WRL::ComPtr<IUIAutomationPropertyChangedEventHandler> handler;};
    std::vector<Listener> listeners;
    Client(){++clientsLive;}
    ~Client(){if(!listeners.empty())++unexpected;--clientsLive;}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid,void** out)override{if(!out)return E_POINTER;*out=nullptr;if(iid==__uuidof(IUnknown)||iid==__uuidof(IUIAutomation)||iid==__uuidof(IUIAutomation2)){*out=static_cast<IUIAutomation2*>(this);AddRef();return S_OK;}return E_NOINTERFACE;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE ElementFromHandle(UIA_HWND,IUIAutomationElement** out)override{if(!out)return E_POINTER;++rootCalls;if(callHook)callHook("root");*out=new Element(L"root");return S_OK;}
    HRESULT STDMETHODCALLTYPE CreatePropertyCondition(PROPERTYID property,VARIANT value,IUIAutomationCondition** out)override{if(property!=UIA_AutomationIdPropertyId||value.vt!=VT_BSTR||!out){++unexpected;return E_INVALIDARG;}*out=new Condition(value.bstrVal);return S_OK;}
    HRESULT STDMETHODCALLTYPE AddPropertyChangedEventHandlerNativeArray(IUIAutomationElement* element,TreeScope scope,IUIAutomationCacheRequest*,IUIAutomationPropertyChangedEventHandler* handler,PROPERTYID* props,int count)override{if(scope!=TreeScope_Element||count!=1||props[0]!=UIA_NamePropertyId){++unexpected;return E_INVALIDARG;}listeners.push_back({element,handler});++subscriptions;return S_OK;}
    HRESULT STDMETHODCALLTYPE RemovePropertyChangedEventHandler(IUIAutomationElement* element,IUIAutomationPropertyChangedEventHandler* handler)override{const auto it=std::find_if(listeners.begin(),listeners.end(),[&](const Listener& x){return x.element.Get()==element&&x.handler.Get()==handler;});if(it==listeners.end()){++unexpected;return E_INVALIDARG;}listeners.erase(it);++removed;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_AutoSetFocus(BOOL* out)override{*out=focus;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_AutoSetFocus(BOOL value)override{focus=value;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_ConnectionTimeout(DWORD* out)override{*out=connection;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_ConnectionTimeout(DWORD value)override{connection=value;if(value!=600)++unexpected;return S_OK;}
    HRESULT STDMETHODCALLTYPE get_TransactionTimeout(DWORD* out)override{*out=transaction;return S_OK;}
    HRESULT STDMETHODCALLTYPE put_TransactionTimeout(DWORD value)override{transaction=value;if(value!=600)++unexpected;return S_OK;}
#include "lossless_uia_client_stubs.inc"
};
inline HRESULT WINAPI initialize(LPVOID value,DWORD mode){const auto hr=::CoInitializeEx(value,mode);if(SUCCEEDED(hr))++initCalls;return hr;}
inline void WINAPI uninitialize(){++uninitCalls;::CoUninitialize();}
inline HRESULT WINAPI create(REFCLSID cls,LPUNKNOWN outer,DWORD context,REFIID iid,LPVOID* out){++factoryCalls;if(factoryHook)factoryHook();if(cls!=CLSID_CUIAutomation8||outer||context!=CLSCTX_INPROC_SERVER){++unexpected;return E_INVALIDARG;}*out=nullptr;auto count=factoryFailures.load();if(count&&factoryFailures.compare_exchange_strong(count,count-1))return E_FAIL;auto* client=new Client;const auto hr=client->QueryInterface(iid,out);client->Release();return hr;}
inline HANDLE WINAPI open(DWORD rights,BOOL inherit,DWORD pid){if(pid!=GetCurrentProcessId()||inherit){++unexpected;SetLastError(ERROR_ACCESS_DENIED);return nullptr;}const auto h=::OpenProcess(rights,inherit,pid);if(h){std::lock_guard<std::mutex> lock(handlesMutex);ownedProcesses.insert(h);++processOpened;}return h;}
inline BOOL WINAPI close(HANDLE h){{std::lock_guard<std::mutex> lock(handlesMutex);if(ownedProcesses.erase(h))++processClosed;}return ::CloseHandle(h);}
inline BOOL WINAPI imagePath(HANDLE h,DWORD flags,LPWSTR out,PDWORD size){if(GetProcessId(h)!=GetCurrentProcessId()||flags||*size<=std::size(image)-1){++unexpected;return FALSE;}std::copy_n(image,std::size(image),out);*size=DWORD(std::size(image)-1);return TRUE;}
inline HWND window(DWORD pid){if(pid!=GetCurrentProcessId())++unexpected;return reinterpret_cast<HWND>(std::uintptr_t{0x7171});}
inline BOOL WINAPI readEnumerateWindows(WNDENUMPROC visitor,LPARAM context){return visitor(window(GetCurrentProcessId()),context);}
inline DWORD WINAPI readWindowProcess(HWND value,LPDWORD pid){if(value!=window(GetCurrentProcessId())){++unexpected;return 0;}if(pid)*pid=GetCurrentProcessId();return GetCurrentThreadId();}
inline int WINAPI readWindowTitle(HWND value,LPWSTR output,int maximum){if(value!=window(GetCurrentProcessId())){++unexpected;return 0;}constexpr wchar_t text[]=L"Lossless Scaling";if(maximum<int(std::size(text)))return 0;std::copy_n(text,std::size(text),output);return int(std::size(text)-1);}
inline int WINAPI readWindowClass(HWND value,LPWSTR output,int maximum){if(value!=window(GetCurrentProcessId())){++unexpected;return 0;}constexpr wchar_t text[]=L"HwndWrapper[LosslessScaling;Fixture;]";if(maximum<int(std::size(text)))return 0;std::copy_n(text,std::size(text),output);return int(std::size(text)-1);}
inline void reset(){initCalls=uninitCalls=factoryCalls=factoryFailures=clientsLive=elementsLive=conditionsLive=subscriptions=removed=processOpened=processClosed=unexpected=0;factoryHook={};callHook={};propertyNameCalls=findCalls=rootCalls=0;}
}
#define CoInitializeEx ymcc::losslessUiaFixture::initialize
#define CoUninitialize ymcc::losslessUiaFixture::uninitialize
#define CoCreateInstance ymcc::losslessUiaFixture::create
#define QueryFullProcessImageNameW ymcc::losslessUiaFixture::imagePath
#define OpenProcess ymcc::losslessUiaFixture::open
#define CloseHandle ymcc::losslessUiaFixture::close

#if defined(YMCC_LOSSLESS_RUNTIME_READ_ONLY)
#define EnumWindows ymcc::losslessUiaFixture::readEnumerateWindows
#define GetWindowThreadProcessId ymcc::losslessUiaFixture::readWindowProcess
#define GetWindowTextW ymcc::losslessUiaFixture::readWindowTitle
#define GetClassNameW ymcc::losslessUiaFixture::readWindowClass
#endif
