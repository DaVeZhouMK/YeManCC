#pragma once
#include "process_efficiency.h"
#include <WebView2.h>
#include <wrl.h>
#include <string_view>
namespace ymcc::processEfficiency {
struct WebViewProcessHandle {HANDLE value=nullptr;~WebViewProcessHandle(){if(value)CloseHandle(value);}};
inline void applyWebViewProcesses(ICoreWebView2Environment* environment)noexcept{
    const DWORD saved=GetLastError();
    try{
        Microsoft::WRL::ComPtr<ICoreWebView2Environment8> newer;
        if(environment&&SUCCEEDED(environment->QueryInterface(IID_PPV_ARGS(&newer)))){
            Microsoft::WRL::ComPtr<ICoreWebView2ProcessInfoCollection> collection;
            if(SUCCEEDED(newer->GetProcessInfos(&collection))&&collection){
                UINT count=0;
                if(SUCCEEDED(collection->get_Count(&count)))for(UINT index=0;index<count;++index){
                    Microsoft::WRL::ComPtr<ICoreWebView2ProcessInfo> info;INT32 pid=0;
                    if(FAILED(collection->GetValueAtIndex(index,&info))||!info||FAILED(info->get_ProcessId(&pid))||pid<=0)continue;
                    WebViewProcessHandle owner{OpenProcess(PROCESS_SET_INFORMATION|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,static_cast<DWORD>(pid))};
                    const auto process=owner.value;
                    if(!process)continue;
                    std::array<wchar_t,32768> path{};DWORD length=static_cast<DWORD>(path.size());
                    if(QueryFullProcessImageNameW(process,0,path.data(),&length)){
                        const auto image=normalized(std::wstring_view(path.data(),length));
                        const auto slash=image.find_last_of(L'\\');
                        if(image.substr(slash==image.npos?0:slash+1)==L"msedgewebview2.exe")apply(process);
                    }
                }
            }
        }
    }catch(...){/* Scheduling policy cannot break WebView creation/recovery. */}
    SetLastError(saved);
}
inline void attachWebViewEfficiency(ICoreWebView2Environment* environment)noexcept{
    try{
        Microsoft::WRL::ComPtr<ICoreWebView2Environment8> newer;
        if(environment&&SUCCEEDED(environment->QueryInterface(IID_PPV_ARGS(&newer)))){
            EventRegistrationToken token{};
            // Handler uses sender, never captures the environment and creates a
            // COM reference cycle. Environment destruction owns unregistration.
            newer->add_ProcessInfosChanged(Microsoft::WRL::Callback<ICoreWebView2ProcessInfosChangedEventHandler>(
                [](ICoreWebView2Environment* sender,IUnknown*)->HRESULT{applyWebViewProcesses(sender);return S_OK;}).Get(),&token);
            applyWebViewProcesses(environment);
        }
    }catch(...){/* No retry timer, process scan or creation failure. */}
}
} // namespace ymcc::processEfficiency