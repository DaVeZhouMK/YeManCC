#pragma once
#include "virtual_target_policy.h"
#include <windows.h>
#include <winver.h>
#include <string>
#include <vector>

#pragma comment(lib,"version.lib")

namespace ymcc::hidHideInstalledVersion {
using input::virtualTargetPolicy::HidHideVersion;

inline HidHideVersion readRegisteredDriverFile() {
    constexpr wchar_t service[] = L"SYSTEM\\CurrentControlSet\\Services\\HidHide";
    DWORD type=0, bytes=sizeof(type);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,service,L"Type",RRF_RT_REG_DWORD|RRF_SUBKEY_WOW6464KEY,
            nullptr,&type,&bytes)!=ERROR_SUCCESS || type!=SERVICE_KERNEL_DRIVER) return {};

    wchar_t system[MAX_PATH]{}, windows[MAX_PATH]{}, image[MAX_PATH]{};
    const auto systemLen=GetSystemDirectoryW(system,MAX_PATH), windowsLen=GetWindowsDirectoryW(windows,MAX_PATH);
    if (!systemLen || systemLen>=MAX_PATH || !windowsLen || windowsLen>=MAX_PATH) return {};
    const auto expected=std::wstring(system)+L"\\drivers\\HidHide.sys";
    bytes=sizeof(image);
    if (RegGetValueW(HKEY_LOCAL_MACHINE,service,L"ImagePath",
            RRF_RT_REG_SZ|RRF_RT_REG_EXPAND_SZ|RRF_SUBKEY_WOW6464KEY,nullptr,image,&bytes)!=ERROR_SUCCESS) return {};
    std::wstring path(image);
    if (path.size()>=2 && path.front()==L'"' && path.back()==L'"') path=path.substr(1,path.size()-2);
    if (_wcsnicmp(path.c_str(),L"\\SystemRoot\\",12)==0) path=std::wstring(windows)+path.substr(11);
    else if (_wcsnicmp(path.c_str(),L"System32\\",9)==0) path=std::wstring(windows)+L"\\"+path;
    else if (path.starts_with(L"\\??\\")) path=path.substr(4);
    // Do not read version resources from an arbitrary registered path or a staged INF.
    if (_wcsicmp(path.c_str(),expected.c_str())!=0) return {};

    DWORD ignored=0;
    const auto size=GetFileVersionInfoSizeW(expected.c_str(),&ignored);
    if (!size || size>1024*1024) return {};
    std::vector<BYTE> data(size);
    if (!GetFileVersionInfoW(expected.c_str(),0,size,data.data())) return {};
    VS_FIXEDFILEINFO* info=nullptr;
    UINT len=0;
    if (!VerQueryValueW(data.data(),L"\\",reinterpret_cast<void**>(&info),&len) || !info ||
            len<sizeof(*info) || info->dwSignature!=0xfeef04bd) return {};
    return {HIWORD(info->dwFileVersionMS),LOWORD(info->dwFileVersionMS),
            HIWORD(info->dwFileVersionLS),LOWORD(info->dwFileVersionLS)};
}

// Held for the app session: a replaced file alone does not prove the running filter changed.
inline const HidHideVersion& registeredFileVersion() noexcept {
    static const auto version=[]() noexcept -> HidHideVersion {
        try { return readRegisteredDriverFile(); } catch (...) { return {}; }
    }();
    return version;
}

inline bool tooOld() noexcept {
    return input::virtualTargetPolicy::hidHideDriverTooOld(registeredFileVersion());
}

inline bool repairPending() noexcept {
    DWORD bytes=0;
    const auto code=RegGetValueW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\YeManCC\\HidHideMaintenance",L"State",
        RRF_RT_ANY|RRF_SUBKEY_WOW6464KEY,nullptr,nullptr,&bytes);
    return code!=ERROR_FILE_NOT_FOUND && code!=ERROR_PATH_NOT_FOUND;
}

inline const std::string& unsupportedVersion() {
    static const auto text=[] {
        if (!tooOld()) return std::string{};
        const auto& v=registeredFileVersion();
        return std::to_string(v[0])+"."+std::to_string(v[1])+"."+std::to_string(v[2])+"."+std::to_string(v[3]);
    }();
    return text;
}
}
