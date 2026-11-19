#pragma once
#include "steam_launch_observation.h"
#include "process_inventory.h"

namespace ymcc::steaminfo::launchDetail {
// Always take the original fresh, complete census. An absent, cancelled or
// retired producer never converts a cached/partial result to absence.
template<class Current,class Fresh>
steamlaunch::LaunchObservation observe(DWORD suspendedChild,bool includeAddons,Current&& current,Fresh&& fresh) noexcept {
    try {
        if(!current())return {};
        const auto table=fresh();
        if(!table||table->status!=processInventory::Status::Complete)return {};
        const auto it=std::find_if(table->rows.begin(),table->rows.end(),[suspendedChild](const auto& row){
            return (!suspendedChild||row.pid!=suspendedChild)&&_wcsicmp(row.name.c_str(),L"steam.exe")==0;
        });
        if(!current())return {};
        return {it==table->rows.end()?0:1,includeAddons?table:nullptr};
    }catch(...){return {};}
}
}
