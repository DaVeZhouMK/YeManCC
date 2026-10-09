#pragma once
// First-enable preparation from bundled, pinned resources. No network/installer/Steam restart.
#include <windows.h>
#include <filesystem>
#include <functional>
#include <string>
#include "json.hpp"
namespace ymcc::deckysetup {
using Json = nlohmann::json;
inline constexpr const char* loaderSha = "1d8e06921ced35b0349e677207953d90e548ea254079150d99ced4d360381824";
using Digest = std::function<std::string(const std::wstring&)>;
inline Json inspect(const std::filesystem::path& steam, const std::filesystem::path& home, const Digest& digest) {
    Json result={{"ready",false},{"bundled",true},{"pythonRequired",false},{"nodeRequired",false},{"createdSteamDebugMarker",false},{"reason","steam-not-installed"}};
    try {
        if(steam.empty() || !std::filesystem::is_regular_file(steam/L"steam.exe"))return result;
        const auto loader=home/L"PluginLoader_noconsole.exe";
        if(!std::filesystem::is_regular_file(loader)){result["reason"]="resource-missing";return result;}
        if(digest(loader.wstring())!=loaderSha){result["reason"]="resource-hash-mismatch";return result;}
        for(const auto& name:{L"plugin.json",L"dist\\index.js"}) if(!std::filesystem::is_regular_file(home/L"plugins"/L"ymcc-sidebar"/name)){
            result["reason"]="plugin-missing";return result;
        }
        const auto marker=steam/L".cef-enable-remote-debugging";
        const DWORD attributes=GetFileAttributesW(marker.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){
            const auto error=GetLastError();
            result["reason"]=error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?"steam-debug-missing":"steam-debug-access-denied";
            return result;
        }
        if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){result["reason"]="steam-debug-unsafe-path";return result;}
        result["ready"]=true;result["reason"]="ready";return result;
    } catch(...){result["reason"]="environment-check-failed";return result;}
}
inline Json prepare(const std::filesystem::path& steam, const std::filesystem::path& home, const Digest& digest, bool steamRunning) {
    auto result=inspect(steam,home,digest);
    if(result["reason"]!="steam-debug-missing")return result;
    // CREATE_NEW is atomic/idempotent and cannot overwrite an existing marker or user content.
    const auto marker=steam/L".cef-enable-remote-debugging";
    HANDLE file=CreateFileW(marker.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){
        const auto error=GetLastError();
        if(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS)return inspect(steam,home,digest);
        result["reason"]=error==ERROR_ACCESS_DENIED?"steam-debug-access-denied":"steam-debug-create-failed";result["error"]=error;return result;
    }
    CloseHandle(file);result=inspect(steam,home,digest);result["createdSteamDebugMarker"]=true;
    if(steamRunning){result["ready"]=false;result["reason"]="steam-restart-required";}
    return result;
}
}
