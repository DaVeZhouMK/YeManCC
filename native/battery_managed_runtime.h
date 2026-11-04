#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>
#include <vector>
#include "json.hpp"

// .NET's documented native-host ABI. DLL paths are absolute; no executable,
// pipe, parent-process monitor or shell command is involved. The CLR and its
// delegates remain loaded for the YMCC process lifetime (they cannot unload).
// ABI definitions correspond to hostfxr.h/coreclr_delegates.h in the .NET SDK.
namespace ymcc::batterycharge::managed {
using Json=nlohmann::json;
using Initialize=int32_t(__cdecl*)(const wchar_t*,const void*,void**);
using Delegate=int32_t(__cdecl*)(void*,int,void**);
using Close=int32_t(__cdecl*)(void*);
using LoadAssembly=int(__stdcall*)(const wchar_t*,const wchar_t*,const wchar_t*,const wchar_t*,void*,void**);
using Invoke=int(__cdecl*)(const unsigned char*,int,unsigned char**,int*);
using Free=void(__cdecl*)(unsigned char*);
inline std::wstring registryDotnetRoot(){
    HKEY key=nullptr;
    if(RegOpenKeyExW(HKEY_LOCAL_MACHINE,L"SOFTWARE\\dotnet\\Setup\\InstalledVersions\\x64",0,KEY_READ|KEY_WOW64_64KEY,&key)!=ERROR_SUCCESS)return {};
    DWORD bytes=0,type=0;auto code=RegQueryValueExW(key,L"InstallLocation",nullptr,&type,nullptr,&bytes);
    std::wstring root;
    if(code==ERROR_SUCCESS&&(type==REG_SZ||type==REG_EXPAND_SZ)&&bytes>=sizeof(wchar_t)&&bytes<=32768*sizeof(wchar_t)){
        std::vector<wchar_t> value(bytes/sizeof(wchar_t)+1,L'\0');
        if(RegQueryValueExW(key,L"InstallLocation",nullptr,&type,reinterpret_cast<BYTE*>(value.data()),&bytes)==ERROR_SUCCESS)root=value.data();
    }
    RegCloseKey(key);return root;
}
inline std::filesystem::path hostFxrPath(){
    std::vector<std::filesystem::path> roots;const auto registered=registryDotnetRoot();
    if(!registered.empty())roots.emplace_back(registered);
    wchar_t programFiles[32768]{};
    if(GetEnvironmentVariableW(L"ProgramW6432",programFiles,32768)||GetEnvironmentVariableW(L"ProgramFiles",programFiles,32768))roots.emplace_back(std::filesystem::path(programFiles)/L"dotnet");
    for(const auto& root:roots){
        if(!root.is_absolute())continue;
        std::error_code error;const auto directory=root/L"host"/L"fxr";
        std::filesystem::directory_iterator it(directory,error),end;
        std::array<unsigned,3> best{};std::filesystem::path selected;
        for(;!error&&it!=end;it.increment(error)){
            const auto name=it->path().filename().wstring();unsigned a=0,b=0,c=0;wchar_t extra=L'\0';
            if(swscanf_s(name.c_str(),L"%u.%u.%u%c",&a,&b,&c,&extra,1)!=3||a<10)continue;
            const std::array<unsigned,3> version{a,b,c};const auto dll=it->path()/L"hostfxr.dll";
            if(version>best&&std::filesystem::is_regular_file(dll,error)){best=version;selected=dll;}
            error.clear();
        }
        if(!selected.empty())return selected;
    }
    throw std::runtime_error("缺少 .NET 10 桌面运行时，无法加载内置充电组件");
}
class Component {
    HMODULE fxr_=nullptr;
    Invoke invoke_=nullptr;Free free_=nullptr;
    std::filesystem::path assembly_;
public:
    // Caller serializes loading/calls with the existing battery request owner.
    void load(const std::wstring& powerRoot){
        const auto directory=std::filesystem::path(powerRoot)/L"battery-host";
        const auto assembly=std::filesystem::absolute(directory/L"YeManBatteryHost.dll").lexically_normal();
        if(invoke_){if(assembly!=assembly_)throw std::runtime_error("充电组件路径已变化，请重启 YMCC");return;}
        const auto config=directory/L"YeManBatteryHost.runtimeconfig.json";
        if(!std::filesystem::is_regular_file(assembly)||!std::filesystem::is_regular_file(config))throw std::runtime_error("缺少内置充电组件，请更新完整程序包");
        if(!fxr_){const auto fxr=hostFxrPath();fxr_=LoadLibraryExW(fxr.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);}
        if(!fxr_)throw std::runtime_error("无法加载 .NET 运行时");
        const auto initialize=reinterpret_cast<Initialize>(GetProcAddress(fxr_,"hostfxr_initialize_for_runtime_config"));
        const auto getDelegate=reinterpret_cast<Delegate>(GetProcAddress(fxr_,"hostfxr_get_runtime_delegate"));
        const auto close=reinterpret_cast<Close>(GetProcAddress(fxr_,"hostfxr_close"));
        if(!initialize||!getDelegate||!close)throw std::runtime_error(".NET 运行时接口不完整");
        void* context=nullptr;const auto code=initialize(config.c_str(),nullptr,&context);
        if(code<0||!context){if(context)close(context);throw std::runtime_error("内置充电组件运行时初始化失败，请检查 .NET 10 桌面运行时");}
        void* loader=nullptr;const auto delegated=getDelegate(context,5,&loader);close(context);
        if(delegated<0||!loader)throw std::runtime_error("内置充电组件加载接口不可用");
        const auto loadAssembly=reinterpret_cast<LoadAssembly>(loader);
        const auto unmanaged=reinterpret_cast<const wchar_t*>(static_cast<intptr_t>(-1));
        void *invoke=nullptr,*release=nullptr;
        if(loadAssembly(assembly.c_str(),L"YeManBatteryHost.InProcessEntry, YeManBatteryHost",L"Invoke",unmanaged,nullptr,&invoke)!=0||!invoke||
           loadAssembly(assembly.c_str(),L"YeManBatteryHost.InProcessEntry, YeManBatteryHost",L"Free",unmanaged,nullptr,&release)!=0||!release)
            throw std::runtime_error("内置充电组件与主程序接口不匹配，请更新完整程序包");
        assembly_=assembly;free_=reinterpret_cast<Free>(release);invoke_=reinterpret_cast<Invoke>(invoke);
    }
    bool loaded()const noexcept{return invoke_!=nullptr;}
    Json call(const Json& envelope){
        if(!invoke_)throw std::runtime_error("内置充电组件尚未加载");
        const auto text=envelope.dump();if(text.size()>65536)throw std::runtime_error("充电请求过大");
        unsigned char* output=nullptr;int length=0;
        const auto result=invoke_(reinterpret_cast<const unsigned char*>(text.data()),static_cast<int>(text.size()),&output,&length);
        struct Buffer{Free free;unsigned char* value;~Buffer(){if(value)free(value);}} buffer{free_,output};
        if(result!=0||!output||length<=0||length>16384)throw std::runtime_error("内置充电组件响应无效");
        return Json::parse(output,output+length);
    }
};
} // namespace ymcc::batterycharge::managed
