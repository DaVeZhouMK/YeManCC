#pragma once
#include "process_efficiency.h"
#include "process_inventory.h"
#include <tlhelp32.h>
namespace ymcc::processEfficiency {
struct EfficiencyHandle {HANDLE value=nullptr;~EfficiencyHandle(){if(value&&value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
struct StartupReceipt {unsigned found=0,confirmed=0,partial=0;};
inline StartupReceipt applyExistingTasksOnce(processInventory::Query query=processInventory::resolveQuery())noexcept{
    const DWORD saved=GetLastError();StartupReceipt receipt;
    try{
        std::array<wchar_t,32768> owner{};
        const auto size=GetModuleFileNameW(nullptr,owner.data(),static_cast<DWORD>(owner.size()));
        if(!size||size>=owner.size()){SetLastError(saved);return receipt;}
        const DWORD self=GetCurrentProcessId();
        const auto applyCandidate=[&](DWORD pid,std::wstring_view leaf){
            if(!pid||pid==self||!knownTaskName(normalized(leaf)))return;
            // The census is a hint. Validate path and set policy on one handle.
            EfficiencyHandle processOwner{OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|PROCESS_SET_INFORMATION,FALSE,pid)};
            const auto process=processOwner.value;
            if(!process)return;
            std::array<wchar_t,32768> image{};DWORD length=static_cast<DWORD>(image.size());
            if(QueryFullProcessImageNameW(process,0,image.data(),&length)&&ownedImage(std::wstring_view(image.data(),length),std::wstring_view(owner.data(),size))){
                ++receipt.found;const auto result=apply(process);
                if(result.qos&&result.priority)++receipt.confirmed;else ++receipt.partial;
            }
        };
        std::shared_ptr<const processInventory::Snapshot> basic;
        if(query){
            try{
                // Fresh and local: neither consume nor publish shared inventory.
                processInventory::Store inventory;
                basic=inventory.read(query,processInventory::currentTime,0,false);
            }catch(...){/* Fall back before applying policy to any candidate. */}
        }
        if(basic&&basic->basicStatus==processInventory::Status::Complete){
            for(const auto& row:basic->rows)applyCandidate(row.pid,row.name);
        }else{
            EfficiencyHandle snapshotOwner{CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0)};
            const auto snapshot=snapshotOwner.value;
            if(snapshot&&snapshot!=INVALID_HANDLE_VALUE){
                PROCESSENTRY32W entry{};entry.dwSize=sizeof(entry);
                if(Process32FirstW(snapshot,&entry))do{
                    applyCandidate(entry.th32ProcessID,entry.szExeFile);
                }while(Process32NextW(snapshot,&entry));
            }
        }
    }catch(...){/* Best effort at normal startup; no timer or retry loop. */}
    SetLastError(saved);return receipt;
}
} // namespace ymcc::processEfficiency
