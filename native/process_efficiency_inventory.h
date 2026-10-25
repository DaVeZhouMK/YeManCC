#pragma once
#include "process_efficiency.h"
#include <tlhelp32.h>
namespace ymcc::processEfficiency {
struct EfficiencyHandle {HANDLE value=nullptr;~EfficiencyHandle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct StartupReceipt {unsigned found=0,confirmed=0,partial=0;};
inline StartupReceipt applyExistingTasksOnce()noexcept{
    const DWORD saved=GetLastError();StartupReceipt receipt;
    try{
        std::array<wchar_t,32768> owner{};
        const auto size=GetModuleFileNameW(nullptr,owner.data(),static_cast<DWORD>(owner.size()));
        if(!size||size>=owner.size()){SetLastError(saved);return receipt;}
        EfficiencyHandle snapshotOwner{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
        const auto snapshot=snapshotOwner.value;
        if(snapshot==INVALID_HANDLE_VALUE){SetLastError(saved);return receipt;}
        PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
        if(Process32FirstW(snapshot,&entry))do{
            if(entry.th32ProcessID==GetCurrentProcessId())continue;
            // The leaf check occurs before opening any handle; path identity
            // is then validated using the same pinned handle that is changed.
            const auto name=normalized(entry.szExeFile);
            if(!knownTaskName(name))continue;
            EfficiencyHandle processOwner{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_SET_INFORMATION,FALSE,entry.th32ProcessID)};
            const auto process=processOwner.value;
            if(!process)continue;
            std::array<wchar_t,32768> image{};DWORD length=static_cast<DWORD>(image.size());
            if(QueryFullProcessImageNameW(process,0,image.data(),&length)&&ownedImage(std::wstring_view(image.data(),length),std::wstring_view(owner.data(),size))){
                ++receipt.found;const auto result=apply(process);
                if(result.qos&&result.priority)++receipt.confirmed;else ++receipt.partial;
            }
        }while(Process32NextW(snapshot,&entry));
    }catch(...){/* Best effort at normal startup; no timer or retry loop. */}
    SetLastError(saved);return receipt;
}
} // namespace ymcc::processEfficiency