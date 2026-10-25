#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <cwctype>
#include <string>
#include <string_view>

namespace ymcc::processEfficiency {
// Windows efficiency mode: explicit EcoQoS execution-speed policy plus Idle
// process priority. One-shot changes only; no affinity/timer/polling changes.
inline constexpr DWORD executionSpeed=1,version=1;
struct Throttling {ULONG Version=version,ControlMask=0,StateMask=0;};
struct Receipt {bool qos=false,priority=false;DWORD qosError=0,priorityError=0;};
struct WindowsOps {
    bool query(HANDLE process,Throttling& state)const noexcept{return GetProcessInformation(process,ProcessPowerThrottling,&state,sizeof(state))!=FALSE;}
    bool setQos(HANDLE process,const Throttling& state)const noexcept{return SetProcessInformation(process,ProcessPowerThrottling,const_cast<Throttling*>(&state),sizeof(state))!=FALSE;}
    DWORD priority(HANDLE process)const noexcept{return GetPriorityClass(process);}
    bool setPriority(HANDLE process)const noexcept{return SetPriorityClass(process,IDLE_PRIORITY_CLASS)!=FALSE;}
    DWORD error()const noexcept{return GetLastError();}
};
template<class Ops> Receipt apply(Ops& ops,HANDLE process)noexcept {
    Receipt receipt;Throttling state{};
    const bool known=ops.query(process,state);
    if(!known)receipt.qosError=ops.error();
    else if((state.ControlMask&executionSpeed)&&(state.StateMask&executionSpeed))receipt.qos=true;
    else {
        state.Version=version;state.ControlMask|=executionSpeed;state.StateMask|=executionSpeed;
        if(ops.setQos(process,state)){
            Throttling check{};
            receipt.qos=ops.query(process,check)&&(check.ControlMask&executionSpeed)&&(check.StateMask&executionSpeed);
        }
        if(!receipt.qos)receipt.qosError=ops.error();
    }
    if(ops.priority(process)==IDLE_PRIORITY_CLASS)receipt.priority=true;
    else if(ops.setPriority(process))receipt.priority=ops.priority(process)==IDLE_PRIORITY_CLASS;
    if(!receipt.priority)receipt.priorityError=ops.error();
    return receipt;
}
inline Receipt apply(HANDLE process)noexcept {
    const DWORD saved=GetLastError();WindowsOps ops;const auto result=apply(ops,process);SetLastError(saved);return result;
}
inline Receipt applyCurrent()noexcept{return apply(GetCurrentProcess());}
inline std::wstring normalized(std::wstring_view input){
    std::wstring value(input);for(auto& c:value){if(c==L'/')c=L'\\';else c=static_cast<wchar_t>(towlower(c));}
    return value;
}
inline constexpr std::array<std::wstring_view,18> taskNames{L"yemancc.exe",L"yemanrecoveryservice.exe",L"yemancontrollervisibilityprobe.exe",L"yemaninputhost.exe",L"yemanfanhost.exe",L"yemanlightsetter.exe",L"yemanbatteryhost.exe",L"hidmaestrosetup.exe",L"memoryscanhost.exe",L"bridge32.exe",L"bridge64.exe",L"yemantdpctl.exe",L"customsteamlibrary.exe",L"steamartworklab.exe",L"pluginloader_noconsole.exe",L"ymccrtssprofilehelper.exe",L"physpanel.exe",L"memreduct.exe"};
inline bool knownTaskName(std::wstring_view name)noexcept{return std::find(taskNames.begin(),taskNames.end(),name)!=taskNames.end();}
inline bool ownedImage(std::wstring_view image,std::wstring_view owner){
    const auto path=normalized(image),module=normalized(owner);
    const auto slash=path.find_last_of(L'\\'),ownerSlash=module.find_last_of(L'\\');
    if(slash==path.npos||ownerSlash==module.npos)return false;
    const auto leaf=std::wstring_view(path).substr(slash+1);

    if(!knownTaskName(leaf))return false;
    auto root=module.substr(0,ownerSlash);
    if(const auto pc=root.find(L"\\powercontrol\\");pc!=root.npos)root.resize(pc);
    else {
        const auto last=root.find_last_of(L'\\');const auto folder=root.substr(last==root.npos?0:last+1);
        if(folder==L"yemancc"||folder==L"customsteamlibrary"||folder==L"powercontrol")root.resize(last);
    }
    return path.size()>root.size()+1&&path.compare(0,root.size(),root)==0&&path[root.size()]==L'\\';
}
inline void applyOwnedChild(HANDLE process)noexcept{
    const DWORD saved=GetLastError();
    try {
        std::array<wchar_t,32768> image{},owner{};DWORD size=static_cast<DWORD>(image.size());
        const auto length=GetModuleFileNameW(nullptr,owner.data(),static_cast<DWORD>(owner.size()));
        if(length&&length<owner.size()&&QueryFullProcessImageNameW(process,0,image.data(),&size)&&ownedImage(std::wstring_view(image.data(),size),std::wstring_view(owner.data(),length)))apply(process);
    }catch(...){/* A scheduling hint must not fail or alter launch ownership. */}
    SetLastError(saved);
}
inline HINSTANCE shellExecute(HWND window,LPCWSTR operation,LPCWSTR file,LPCWSTR parameters,LPCWSTR directory,INT show)noexcept{
    const DWORD saved=GetLastError();
    SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;
    info.hwnd=window;info.lpVerb=operation;info.lpFile=file;info.lpParameters=parameters;info.lpDirectory=directory;info.nShow=show;
    FILETIME boundary{};GetSystemTimeAsFileTime(&boundary);
    if(!ShellExecuteExW(&info)){
        const auto code=reinterpret_cast<INT_PTR>(info.hInstApp);
        return reinterpret_cast<HINSTANCE>(code>0&&code<=32?code:INT_PTR(SE_ERR_ACCESSDENIED));
    }
    if(info.hProcess){
        const DWORD after=GetLastError();
        // Only a newly created process belongs to this launch. Existing URL,
        // browser, Explorer or other activation targets retain their policies.
        FILETIME created{},exited{},kernel{},user{};
        if(GetProcessTimes(info.hProcess,&created,&exited,&kernel,&user)&&CompareFileTime(&created,&boundary)>=0){
            if(GetPriorityClass(info.hProcess)==IDLE_PRIORITY_CLASS)SetPriorityClass(info.hProcess,NORMAL_PRIORITY_CLASS);
            applyOwnedChild(info.hProcess);
        }
        CloseHandle(info.hProcess);SetLastError(after);
    }
    SetLastError(saved);
    return reinterpret_cast<HINSTANCE>(INT_PTR(33));
}
inline BOOL createProcess(LPCWSTR application,LPWSTR command,LPSECURITY_ATTRIBUTES processAttributes,LPSECURITY_ATTRIBUTES threadAttributes,BOOL inherit,DWORD flags,LPVOID environment,LPCWSTR directory,LPSTARTUPINFOW startup,LPPROCESS_INFORMATION info)noexcept{
    // Never let an explicitly launched game/tool inherit the main UI's Idle
    // class. Self-owned tasks are opted back into efficiency via their handle.
    constexpr DWORD priorityFlags=IDLE_PRIORITY_CLASS|BELOW_NORMAL_PRIORITY_CLASS|NORMAL_PRIORITY_CLASS|ABOVE_NORMAL_PRIORITY_CLASS|HIGH_PRIORITY_CLASS|REALTIME_PRIORITY_CLASS;
    if(!(flags&priorityFlags))flags|=NORMAL_PRIORITY_CLASS;
    const BOOL created=CreateProcessW(application,command,processAttributes,threadAttributes,inherit,flags,environment,directory,startup,info);
    if(created&&info&&info->hProcess)applyOwnedChild(info->hProcess);
    return created;
}
} // namespace ymcc::processEfficiency