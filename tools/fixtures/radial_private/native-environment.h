#pragma once
// Test-only environment terminals. Actual Overlay/Carrier/Surface bodies, clocks,
// input transport, WebView2, Composition and independent input remain compiled.
#include <windows.h>
#include <wrl.h>
#include <WebView2.h>
#include <WebView2EnvironmentOptions.h>
#include <stdexcept>
#include <string>
namespace ymcc::radialfixture {
inline HWND foreground() noexcept { return nullptr; }
inline HMONITOR monitorFromPoint(POINT,DWORD) noexcept { return reinterpret_cast<HMONITOR>(1); }
inline HMONITOR monitorFromWindow(HWND,DWORD) noexcept { return reinterpret_cast<HMONITOR>(1); }
inline BOOL monitorInfo(HMONITOR,LPMONITORINFO value) noexcept {
 if(!value||value->cbSize!=sizeof(MONITORINFO))return FALSE;
 value->rcMonitor={0,0,1280,800};value->rcWork={0,0,1280,800};value->dwFlags=MONITORINFOF_PRIMARY;return TRUE;
}
inline HANDLE denyForeignProcess(DWORD,BOOL,DWORD) noexcept {SetLastError(ERROR_ACCESS_DENIED);return nullptr;}
inline BOOL memorySession(DWORD pid,DWORD* session) noexcept {if(pid!=GetCurrentProcessId()||!session)return FALSE;*session=1;return TRUE;}
inline BOOL ownedWindows(WNDENUMPROC,LPARAM) noexcept {return TRUE;}
inline std::wstring requiredEnvironment(const wchar_t* name){const auto n=GetEnvironmentVariableW(name,nullptr,0);if(!n)throw std::runtime_error("Reviewed explicit WebView2 runtime required");std::wstring value(n,L'\0');GetEnvironmentVariableW(name,value.data(),n);value.resize(n-1);return value;}
inline HRESULT createEnvironment(PCWSTR profile,ICoreWebView2CreateCoreWebView2EnvironmentCompletedHandler* completed){
 const auto runtime=requiredEnvironment(L"YMCC_RADIAL_FIXED_RUNTIME");
 const auto expected=requiredEnvironment(L"YMCC_RADIAL_FIXED_RUNTIME_VERSION");
 LPWSTR actual=nullptr;const auto hr=GetAvailableCoreWebView2BrowserVersionString(runtime.c_str(),&actual);
 if(FAILED(hr)||!actual)return FAILED(hr)?hr:E_FAIL;const bool same=expected==actual;CoTaskMemFree(actual);if(!same)return E_ACCESSDENIED;
 auto options=Microsoft::WRL::Make<CoreWebView2EnvironmentOptions>();
 // Supplemental browser restrictions. They do not prove OS/network/device
 // confinement by themselves; the review contract keeps that limitation explicit.
 options->put_AdditionalBrowserArguments(L"--disable-background-networking --disable-component-update --disable-sync --no-first-run --disable-features=msSmartScreenProtection,WebRtcAllowInputVolumeAdjustment --host-resolver-rules=MAP * ~NOTFOUND --autoplay-policy=user-gesture-required");
 options->put_AllowSingleSignOnUsingOSPrimaryAccount(FALSE);
 return CreateCoreWebView2EnvironmentWithOptions(runtime.c_str(),profile,options.Get(),completed);
}
}
