#pragma once
// Passive media diagnostics. Export only the root already used by this process;
// never select/create a library root, start a controller, or launch a worker.
#include "shared_game_media.h"
#include "steam_log_export.h"
#include <atomic>
namespace ymcc::media::diagnostics {
inline std::mutex observedRootMutex;
inline fs::path observedRoot;
inline void rememberRoot(const fs::path& root){std::lock_guard lock(observedRootMutex);observedRoot=root;}
inline fs::path currentRoot(){std::lock_guard lock(observedRootMutex);return observedRoot;}
struct Export {Json receipt;std::vector<fs::path> files;};
inline Export exportMetadata(const fs::path& destinationDirectory){
    const auto root=currentRoot();
    Export result{{{"schemaVersion",1},{"evidenceScope","per-file-snapshots-of-existing-shared-media-state"},
        {"sourceReadOnly",true},{"networkStarted",false},{"workerStarted",false},{"coherentAcrossFiles",false},
        {"rootObserved",!root.empty()},{"maxBytesPerFile",16ull*1024*1024},{"files",Json::array()}},{}};
    if(!root.empty())result.receipt["dataRoot"]=utf8(root.wstring());
    static std::atomic<unsigned long long> sequence{0};
    for(const auto* name:{L"game-media-downloads.json",L"game-media-download-progress.json",L"game-media.json",L"last-opti-client-supplement.json"}){
        Json item={{"name",utf8(name)},{"present",false},{"copied",false}};
        if(root.empty()){item["missingReason"]="media-root-not-observed";result.receipt["files"].push_back(item);continue;}
        const auto source=root/(std::wstring_view(name)==L"last-opti-client-supplement.json"?L"state":L"config")/name;std::error_code error;
        const auto status=fs::symlink_status(source,error);
        if(error||!fs::exists(status)){
            item["missingReason"]=!error||error==std::errc::no_such_file_or_directory?"not-created-or-not-retained":"file-status-error";
            if(error)item["statusErrorCode"]=error.value();result.receipt["files"].push_back(item);continue;
        }
        item["present"]=true;
        if(!fs::is_regular_file(status)||fs::is_symlink(status)){item["missingReason"]="media-state-not-regular";result.receipt["files"].push_back(item);continue;}
        fs::create_directories(destinationDirectory,error);
        if(error){item["missingReason"]="media-export-directory-failed";item["statusErrorCode"]=error.value();result.receipt["files"].push_back(item);continue;}
        const auto destination=destinationDirectory/name;
        const auto temporary=destinationDirectory/(std::wstring(name)+L".tmp.media-export."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(sequence.fetch_add(1)));
        DWORD code=0;
        if(!ymcc::logexport::snapshotSharedLog(source,temporary,code,16ull*1024*1024)){
            item["missingReason"]=code==ERROR_FILE_TOO_LARGE?"media-state-size-limit":"media-state-copy-failed";item["win32Error"]=code;result.receipt["files"].push_back(item);continue;
        }
        if(!MoveFileExW(ymcc::logexport::winPath(temporary).c_str(),ymcc::logexport::winPath(destination).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
            code=GetLastError();fs::remove(temporary,error);item["missingReason"]="media-state-publish-failed";item["win32Error"]=code;result.receipt["files"].push_back(item);continue;
        }
        const auto bytes=fs::file_size(destination,error);item["copied"]=true;if(!error)item["copiedBytes"]=bytes;
        result.files.push_back(destination);result.receipt["files"].push_back(item);
    }
    return result;
}
} // namespace ymcc::media::diagnostics
