#pragma once
// First-enable preparation from bundled, pinned resources. No network/installer/Steam restart.
#include <winsock2.h>
#include <windows.h>
#include <filesystem>
#include <functional>
#include <fstream>
#include <array>
#include <set>
#include <string>
#include "json.hpp"
#include "decky_loader_identity.h"
#include "decky_sidebar_resource_cache.h"
namespace ymcc::deckysetup {
using Json = nlohmann::json;
// Official bytes only. Generated identity changes through the consumer upgrade tool.
inline constexpr const char* loaderSha = ymcc::deckyidentity::sha256;
inline std::string pathText(const std::filesystem::path& path) {
    const auto value=path.u8string();return std::string(value.begin(),value.end());
}
using Digest = std::function<std::string(const std::wstring&)>;
// A package receipt is written only by the existing YMCC deployment/update lane.
// Read once at preparation, never from a healthy-frame timer. Legacy installs
// retain their pinned Loader check until their next managed whole-package update.
inline constexpr std::array<const wchar_t*,7> managedFiles={L"PluginLoader_noconsole.exe",L"LICENSE.decky-loader",
    L"plugins/ymcc-sidebar/dist/index.js",L"plugins/ymcc-sidebar/plugin.json",L"plugins/ymcc-sidebar/package.json",
    L"plugins/ymcc-sidebar/LICENSE.decky-api",L"plugins/ymcc-sidebar/LICENSE.vue"};
inline bool safeManagedPath(const std::filesystem::path& file) {
    for(auto cursor=file;!cursor.empty();){
        const auto attributes=GetFileAttributesW(cursor.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return false;}
        else if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return false;
        const auto parent=cursor.parent_path();if(parent==cursor)break;cursor=parent;
    }
    return true;
}
inline std::string readManagedContract(const std::filesystem::path& home,Json& result,Json& manifest,const std::filesystem::path& bundledManifest={}) {
    auto receipt=home/L".ymcc-managed.json";
    if(!safeManagedPath(receipt))return "resource-unsafe-path";
    auto attributes=GetFileAttributesW(receipt.c_str());
    result["managedPackageSource"]="receipt";
    if(attributes==INVALID_FILE_ATTRIBUTES){
        const auto error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND&&error!=ERROR_PATH_NOT_FOUND)return "resource-package-invalid";
        // Old YMCC updaters merge the package but cannot write the new installed
        // receipt. Consume the same authenticated package metadata beside YMCC
        // on that FIRST upgrade; never silently fall back from a corrupt receipt.
        if(bundledManifest.empty()){result["managedPackageVerified"]=false;result["managedPackageSource"]="legacy";return {};}
        receipt=bundledManifest;if(!safeManagedPath(receipt))return "resource-unsafe-path";
        attributes=GetFileAttributesW(receipt.c_str());if(attributes==INVALID_FILE_ATTRIBUTES)return "resource-package-invalid";
        result["managedPackageSource"]="package";
    }
    try{
        if((attributes&FILE_ATTRIBUTE_DIRECTORY)||std::filesystem::file_size(receipt)>131072)return "resource-package-invalid";
        std::ifstream input(receipt,std::ios::binary);manifest=Json::parse(input);
        if(manifest.value("schemaVersion",0)!=1||manifest.value("owner",std::string{})!="YMCC"||manifest.value("origin",std::string{})!="official-release"||
            !manifest.contains("files")||!manifest["files"].is_array()||manifest["files"].size()!=managedFiles.size())return "resource-package-invalid";
        if(manifest.value("loaderVersion",std::string{})!=ymcc::deckyidentity::version||manifest.value("loaderSha256",std::string{})!=loaderSha)return "resource-upgrade-required";
        std::set<std::string> seen;
        for(const auto& entry:manifest["files"]){
            const auto name=entry.value("path",std::string{});bool allowed=false;
            for(const auto* expected:managedFiles)if(name==pathText(std::filesystem::path(expected))){allowed=true;break;}
            if(!allowed||!seen.insert(name).second)return "resource-package-invalid";
            const auto expected=entry.value("sha256",std::string{});
            if(expected.size()!=64||expected.find_first_not_of("0123456789abcdef")!=std::string::npos||!entry.contains("bytes")||!entry["bytes"].is_number_integer()||entry["bytes"]<=0||entry["bytes"]>134217728)return "resource-package-invalid";
            if(name=="PluginLoader_noconsole.exe"&&expected!=loaderSha)return "resource-package-invalid";
        }
        return {};
    }catch(...){return "resource-package-invalid";}
}
inline std::string inspectManagedPackage(const std::filesystem::path& home,const Digest& digest,Json& result,const std::filesystem::path& bundledManifest={}) {
    Json manifest;const auto failure=readManagedContract(home,result,manifest,bundledManifest);
    if(!failure.empty()||manifest.is_null())return failure;
    try{
        for(const auto& entry:manifest["files"]){
            const auto name=entry.at("path").get<std::string>();
            const auto target=home/std::filesystem::u8path(name);
            if(!safeManagedPath(target))return "resource-unsafe-path";
            if(!std::filesystem::is_regular_file(target)||std::filesystem::file_size(target)!=entry["bytes"].get<std::uintmax_t>())return "resource-package-mismatch";
            const auto actual=name=="PluginLoader_noconsole.exe"?result.value("loaderObservedSha256",std::string{}):digest(target.wstring());
            if(actual!=entry.at("sha256").get<std::string>())return "resource-package-mismatch";
        }
        result["managedPackageVerified"]=true;return {};
    }catch(...){return "resource-package-invalid";}
}
inline Json inspect(const std::filesystem::path& steam, const std::filesystem::path& home, const Digest& digest,const std::filesystem::path& bundledManifest={}) {
    Json result={{"ready",false},{"bundled",true},{"pythonRequired",false},{"nodeRequired",false},{"createdSteamDebugMarker",false},{"reason","steam-not-installed"}};
    result["steamDirectory"]=pathText(steam);
    result["loaderExpectedSha256"]=loaderSha;
    result["resourceDirectory"]=pathText(home);
    result["loaderPath"]=pathText(home/L"PluginLoader_noconsole.exe");
    result["pluginPath"]=pathText(home/L"plugins"/L"ymcc-sidebar");
    try {
        if(steam.empty() || !std::filesystem::is_regular_file(steam/L"steam.exe"))return result;
        const auto loader=home/L"PluginLoader_noconsole.exe";
        for(const auto* name:managedFiles)if(!safeManagedPath(home/name)){result["reason"]="resource-unsafe-path";return result;}
        std::error_code ec;
        if(!std::filesystem::is_regular_file(loader,ec)){result["reason"]="resource-missing";result["missingPath"]=pathText(loader);result["error"]=ec.value();return result;}
        // Reuse the existing one-time resource digest. This is the checked file,
        // not a claim that the mapped child image was atomically hashed later.
        const auto observed=digest(loader.wstring());
        result["loaderObservedSha256"]=observed;
        result["loaderHashMatched"]=observed==loaderSha;
        if(observed!=loaderSha){result["reason"]="resource-hash-mismatch";return result;}
        result["loaderOrigin"]=ymcc::deckyidentity::origin;
        result["loaderVersion"]=ymcc::deckyidentity::version;
        result["loaderConsumerContract"]=ymcc::deckyidentity::consumerContract;
        result["loaderModified"]=ymcc::deckyidentity::modified;
        for(const auto& name:{L"plugin.json",L"dist\\index.js"}) if(!std::filesystem::is_regular_file(home/L"plugins"/L"ymcc-sidebar"/name)){
            result["reason"]="plugin-missing";result["missingPath"]=pathText(home/L"plugins"/L"ymcc-sidebar"/name);return result;
        }
        const auto packageReason=inspectManagedPackage(home,digest,result,bundledManifest);
        if(!packageReason.empty()){result["reason"]=packageReason;return result;}
        const auto marker=steam/L".cef-enable-remote-debugging";
        const DWORD attributes=GetFileAttributesW(marker.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){
            const auto error=GetLastError();
            result["reason"]=error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?"steam-debug-missing":"steam-debug-access-denied";
            result["error"]=error;return result;
        }
        if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){result["reason"]="steam-debug-unsafe-path";return result;}
        result["ready"]=true;result["reason"]="ready";return result;
    } catch(...){result["reason"]="environment-check-failed";return result;}
}
inline Json prepare(const std::filesystem::path& steam, const std::filesystem::path& home, const Digest& digest, bool steamRunning, const std::function<bool()>& mayPrepare = {},const std::filesystem::path& bundledManifest={},const ymcc::deckyresources::Options& resourceOptions={}) {
    if(mayPrepare&&!mayPrepare())return {{"ready",false},{"reason","preparation-cancelled"},{"createdSteamDebugMarker",false}};
    const auto readEnvironment=[&]{return resourceOptions.isolatedPreparation?resourceOptions.isolatedPreparation():inspect(steam,home,digest,bundledManifest);};
    auto result=readEnvironment();
    Json maintenance=Json::object();
    const auto why=result.value("reason",std::string{});
    if(!resourceOptions.isolatedPreparation&&resourceOptions.enabled&&(why=="ready"||why=="steam-debug-missing"||why=="resource-missing"||why=="resource-hash-mismatch"||why=="plugin-missing"||why=="resource-package-mismatch")){
        Json contract,description;
        const auto invalid=readManagedContract(home,description,contract,bundledManifest);
        if(!invalid.empty()){result["ready"]=false;result["reason"]=invalid;}
        else if(!contract.is_null()){
            const ymcc::deckyresources::SafePath safe=[](const auto& path){return safeManagedPath(path);};
            try{maintenance=ymcc::deckyresources::Cache(home,contract,digest,safe,mayPrepare,resourceOptions).run();}
            catch(...){maintenance={{"resourceCacheAvailable",false},{"resourcesRepaired",0u},{"resourceRepairReason","resource-cache-unavailable"}};}
            if(maintenance.value("resourcesRepaired",0u)>0)result=inspect(steam,home,digest,bundledManifest);
            result.update(maintenance);
        }
    }
    const auto withMaintenance=[&](Json fresh){fresh.update(maintenance);return fresh;};
    // Cancellation after a resource read cannot authorize a late marker write.
    if(mayPrepare&&!mayPrepare()){result["ready"]=false;result["reason"]="preparation-cancelled";return result;}
    if(result["reason"]!="steam-debug-missing")return result;
    // CREATE_NEW is atomic/idempotent and cannot overwrite an existing marker or user content.
    const auto marker=steam/L".cef-enable-remote-debugging";
    HANDLE file=CreateFileW(marker.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE){
        const auto error=GetLastError();
        if(error==ERROR_FILE_EXISTS||error==ERROR_ALREADY_EXISTS)return withMaintenance(readEnvironment());
        result["reason"]=error==ERROR_ACCESS_DENIED?"steam-debug-access-denied":"steam-debug-create-failed";result["error"]=error;return result;
    }
    CloseHandle(file);result=withMaintenance(inspect(steam,home,digest,bundledManifest));result["createdSteamDebugMarker"]=true;
    if(mayPrepare&&!mayPrepare()){result["ready"]=false;result["reason"]="preparation-cancelled";return result;}
    if(steamRunning){result["ready"]=false;result["reason"]="steam-restart-required";}
    return result;
}
}
