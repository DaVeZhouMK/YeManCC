#pragma once
#ifndef YMCC_NOIO_CASE_DIR
#error This port requires the reviewed native launch NoIO adapter
#endif
#include "steam_launch_owner_observation.h"

inline ymcc::steamlaunch::LaunchFacts memorySteamLaunchFacts(){
    return {
        [](DWORD child,bool addons){
            return ymcc::steaminfo::launchDetail::observe(child,addons,[]{return true;},[]{
                return ymcc::processInventory::read(ymcc::processInventory::resolveQuery(),ymcc::processInventory::currentTime,0);
            });
        },
        []() -> std::wstring {
            // Keep the existing reviewed registry memory terminal in use.
            wchar_t path[32768]{};DWORD bytes=sizeof(path);
            if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamExe",RRF_RT_REG_SZ,nullptr,path,&bytes)!=ERROR_SUCCESS)
                return {};
            const auto exe=ymcc::steamlaunch::trim(path);
            return ymcc::steamlaunch::validExe(exe)?exe:std::wstring{};
        }
    };
}
