#pragma once
// Passive media diagnostics. Export only the root already used by this process;
// never select/create a library root, start a controller, or launch a worker.
#include "shared_game_media.h"
#include "steam_log_export.h"
#include <array>
#include <atomic>
namespace ymcc::media::diagnostics {
inline std::mutex observedRootMutex;
inline fs::path observedRoot;
inline void rememberRoot(const fs::path& root){std::lock_guard lock(observedRootMutex);observedRoot=root;}
inline fs::path currentRoot(){std::lock_guard lock(observedRootMutex);return observedRoot;}
struct Export {Json receipt;std::vector<fs::path> files;};
// This is a disk observation, not the Worker's effective queue or a liveness probe.
// Never stage the original: rows can contain executable paths and replay arguments.
inline Json observeNetworkRetryQueue(const fs::path& root){
    constexpr DWORD maxBytes=8u<<20;
    Json receipt={{"scope","persisted-labels-not-worker-liveness"},{"maxBytes",maxBytes}};
    const auto finish=[&](const char* status){receipt["status"]=status;return std::move(receipt);};
    const auto unavailable=[&](DWORD error,bool opening){
        if(opening&&(error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND))return finish("absent");
        receipt["win32Error"]=error;return finish("unreadable");
    };
    if(root.empty())return finish("root-not-observed");
    if(!root.is_absolute())return finish("not-regular");
    const auto state=root/L"state";
    for(const auto& directory:{root,state}){
        const DWORD attributes=GetFileAttributesW(ymcc::logexport::winPath(directory).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES)return unavailable(GetLastError(),true);
        if(!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return finish("not-regular");
    }
    struct Handle {HANDLE value;~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}};
    std::string bytes;
    {
        const Handle file{CreateFileW(ymcc::logexport::winPath(state/L"network-retry-queue.json").c_str(),
            GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr)};
        if(file.value==INVALID_HANDLE_VALUE)return unavailable(GetLastError(),true);
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(file.value,&info))return unavailable(GetLastError(),false);
        if((info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||GetFileType(file.value)!=FILE_TYPE_DISK)
            return finish("not-regular");
        if(info.nFileSizeHigh||info.nFileSizeLow>maxBytes)return finish("size-limit");
        bytes.resize(info.nFileSizeLow);
        DWORD offset=0;
        while(offset<info.nFileSizeLow){
            DWORD read=0;
            if(!ReadFile(file.value,bytes.data()+offset,info.nFileSizeLow-offset,&read,nullptr))return unavailable(GetLastError(),false);
            if(!read)return finish("unreadable");
            offset+=read;
        }
    }
    Json queue;
    struct InvalidQueueJson {};
    try {
        if(bytes.find('\0')!=std::string::npos)return finish("invalid");
        std::array<std::set<std::string>,65> keys;
        queue=Json::parse(bytes,[&](int depth,Json::parse_event_t event,Json& value){
            if(depth<0||depth>64)throw InvalidQueueJson{};
            if(event==Json::parse_event_t::object_start)keys[depth].clear();
            if(event==Json::parse_event_t::key&&(depth==0||!keys[depth-1].insert(value.get_ref<const std::string&>()).second))
                throw InvalidQueueJson{};
            return true;
        });
    }catch(const Json::exception&){return finish("invalid");}
     catch(const InvalidQueueJson&){return finish("invalid");}
    if(!queue.is_object())return finish("invalid");
    const auto version=queue.find("schemaVersion"),jobs=queue.find("jobs"),history=queue.find("history"),quarantine=queue.find("quarantinedJobs");
    if(version!=queue.end()&&(!version->is_number_integer()||*version!=1))return finish("unsupported-version");
    if(jobs==queue.end()||!jobs->is_object()||jobs->size()>4096||
       (history!=queue.end()&&!history->is_array())||(quarantine!=queue.end()&&!quarantine->is_object()))return finish("invalid");
    size_t waiting=0,claimed=0,legacyRunning=0,reviewChanged=0,reviewInterrupted=0,other=0;
    for(const auto& job:*jobs){
        const auto label=job.find("status");
        if(label==job.end()||!label->is_string()){++other;continue;}
        const auto& status=label->get_ref<const std::string&>();
        if(status=="waiting-network")++waiting;
        else if(status=="running-event-wakeup")++claimed;
        else if(status=="running")++legacyRunning;
        else if(status=="needs-manual-review-executable-changed")++reviewChanged;
        else if(status=="needs-manual-review-interrupted-retry")++reviewInterrupted;
        else ++other;
    }
    receipt["counts"]={{"jobs",jobs->size()},{"waitingNetwork",waiting},{"wakeClaimed",claimed},{"legacyRunning",legacyRunning},
        {"reviewExecutableChanged",reviewChanged},{"reviewInterrupted",reviewInterrupted},{"unrecognized",other},
        {"history",history==queue.end()?0:history->size()},{"quarantined",quarantine==queue.end()?0:quarantine->size()}};
    return finish("observed");
}
inline Export exportMetadata(const fs::path& destinationDirectory){
    const auto root=currentRoot();
    Export result{{{"schemaVersion",1},{"evidenceScope","per-file-snapshots-of-existing-shared-media-state"},
        {"sourceReadOnly",true},{"networkStarted",false},{"workerStarted",false},{"coherentAcrossFiles",false},
        {"rootObserved",!root.empty()},{"maxBytesPerFile",16ull*1024*1024},{"files",Json::array()}},{}};
    if(!root.empty())result.receipt["dataRoot"]=utf8(root.wstring());
    result.receipt["networkRetryQueue"]=observeNetworkRetryQueue(root);
    static std::atomic<unsigned long long> sequence{0};
    // Include the library context already cached by the Host. These bytes are
    // historical evidence, never a fresh scan or proof that a game path exists.
    struct MetadataFile { const wchar_t* directory; const wchar_t* name; };
    constexpr MetadataFile metadataFiles[]={
        {L"config",L"game-media-downloads.json"},{L"config",L"game-media-download-progress.json"},
        {L"config",L"game-media.json"},{L"state",L"last-opti-client-supplement.json"},
        // Preserve explicit ID/name/artwork choices as cached diagnostic evidence.
        {L"config",L"manual-overrides.json"},
        {L"state",L"ui-errors.jsonl"},{L"state",L"library-scan.json"},
        {L"state",L"steam-add-plan.json"},{L"state",L"library-steam-add-plan.json"},
        // Account observations and completed actions are cached evidence only.
        {L"state",L"steam-accounts.json"},
        {L"state",L"last-steam-add.json"},
        {L"state",L"last-steam-add-all-accounts.json"},
        {L"state",L"last-steam-artwork-refresh-all-accounts.json"},
        {L"state",L"last-steam-delete-all-accounts.json"},
        // Existing ownership and single-account actions are cached evidence only.
        {L"state",L"ymcc-game-whitelist-membership.json"},
        {L"state",L"last-steam-delete.json"},{L"state",L"last-steam-artwork-refresh.json"},
        {L"state",L"steam-delete-plan.json"},
        {L"",L"data-root.json"},{L"state",L"data-migration.json"}
    };
    for(const auto& file:metadataFiles){
        const auto* name=file.name;
        Json item={{"name",utf8(name)},{"present",false},{"copied",false}};
        if(root.empty()){item["missingReason"]="media-root-not-observed";result.receipt["files"].push_back(item);continue;}
        const auto source=root/file.directory/name;std::error_code error;
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
// Only explicit log export calls this helper. The caller supplies its known
// installed child directory; no library discovery, startup or collection runs.
inline Export exportHostRecords(const fs::path& hostRoot,const fs::path& destinationDirectory){
    constexpr size_t retained=3,scanLimit=128;
    constexpr unsigned long long maxBytes=256ull*1024;
    Export result{{{"schemaVersion",1},{"evidenceScope","existing-child-host-crash-and-exit-records"},
        {"sourceReadOnly",true},{"networkStarted",false},{"workerStarted",false},{"coherentAcrossFiles",false},
        {"rootObserved",!hostRoot.empty()&&hostRoot.is_absolute()},{"maxFilesPerCategory",retained},
        {"maxBytesPerFile",maxBytes},{"maxEntriesPerCategory",scanLimit},{"groups",Json::array()}},{}};
    static std::atomic<unsigned long long> sequence{0};
    for(const auto* kind:{L"crash",L"exit"}){
        Json group={{"kind",utf8(kind)},{"files",Json::array()},{"scanLimitReached",false}};
        if(hostRoot.empty()||!hostRoot.is_absolute()){group["status"]="host-root-not-observed";result.receipt["groups"].push_back(group);continue;}
        const auto logs=hostRoot/L"logs",folder=logs/(L"host-"+std::wstring(kind));bool accessible=true;
        for(const auto& path:{hostRoot,logs,folder}){
            const auto attributes=GetFileAttributesW(ymcc::logexport::winPath(path).c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES){const auto code=GetLastError();group["status"]=code==ERROR_FILE_NOT_FOUND||code==ERROR_PATH_NOT_FOUND?"not-created-or-not-retained":"directory-status-error";group["win32Error"]=code;accessible=false;break;}
            if(attributes&FILE_ATTRIBUTE_REPARSE_POINT||!(attributes&FILE_ATTRIBUTE_DIRECTORY)){group["status"]="host-directory-not-regular";accessible=false;break;}
        }
        if(!accessible){result.receipt["groups"].push_back(group);continue;}
        std::vector<std::pair<fs::file_time_type,fs::path>> candidates;std::error_code error;
        fs::directory_iterator cursor(ymcc::logexport::winPath(folder),error),end;size_t examined=0,skipped=0;
        if(error){group["status"]="directory-enumeration-failed";group["statusErrorCode"]=error.value();result.receipt["groups"].push_back(group);continue;}
        while(cursor!=end){
            if(examined>=scanLimit){group["scanLimitReached"]=true;break;}++examined;
            const auto file=cursor->path();const auto name=file.filename().wstring(),prefix=std::wstring(kind)+L"-";
            if(name.rfind(prefix,0)==0&&name.size()>prefix.size()+5&&name.ends_with(L".json")){
                const auto identity=name.substr(prefix.size(),name.size()-prefix.size()-5);const auto dash=identity.find(L'-');
                const bool named=dash!=std::wstring::npos&&dash>0&&dash+1<identity.size()&&identity.find(L'-',dash+1)==std::wstring::npos&&
                    identity.find_first_not_of(L"0123456789-")==std::wstring::npos;
                if(named){
                    const auto attributes=GetFileAttributesW(ymcc::logexport::winPath(file).c_str());
                    if(attributes==INVALID_FILE_ATTRIBUTES||attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))++skipped;
                    else{const auto time=fs::last_write_time(file,error);if(error){++skipped;error.clear();}else candidates.emplace_back(time,file);}
                }
            }
            cursor.increment(error);if(error){group["enumerationErrorCode"]=error.value();break;}
        }
        group["examinedEntries"]=examined;group["skippedNonRegularOrUnavailable"]=skipped;
        std::sort(candidates.begin(),candidates.end(),[](const auto& left,const auto& right){return left.first!=right.first?left.first>right.first:left.second.filename().wstring()>right.second.filename().wstring();});
        if(candidates.size()>retained)candidates.resize(retained);
        bool partial=group.value("scanLimitReached",false)||group.contains("enumerationErrorCode")||skipped>0;
        for(const auto& candidate:candidates){
            const auto& source=candidate.second;const auto name=L"custom-library-"+source.filename().wstring();
            Json item={{"name",utf8(name)},{"present",true},{"copied",false}};
            fs::create_directories(ymcc::logexport::winPath(destinationDirectory),error);
            if(error){item["missingReason"]="host-export-directory-failed";item["statusErrorCode"]=error.value();error.clear();}
            else{
                const auto destination=destinationDirectory/name;
                const auto temporary=destinationDirectory/(name+L".tmp.host-export."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(sequence.fetch_add(1)));DWORD code=0;
                if(!ymcc::logexport::snapshotSharedLog(source,temporary,code,maxBytes)){item["missingReason"]=code==ERROR_FILE_TOO_LARGE?"host-record-size-limit":"host-record-copy-failed";item["win32Error"]=code;}
                else if(!MoveFileExW(ymcc::logexport::winPath(temporary).c_str(),ymcc::logexport::winPath(destination).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
                    code=GetLastError();fs::remove(ymcc::logexport::winPath(temporary),error);error.clear();item["missingReason"]="host-record-publish-failed";item["win32Error"]=code;
                }else{item["copied"]=true;const auto bytes=fs::file_size(ymcc::logexport::winPath(destination),error);if(!error)item["copiedBytes"]=bytes;error.clear();result.files.push_back(destination);}
            }
            if(!item.value("copied",false))partial=true;group["files"].push_back(item);
        }
        group["status"]=partial?"partial":candidates.empty()?"empty":"copied";result.receipt["groups"].push_back(group);
    }
    return result;
}
// Explicit export only: snapshot the three known installed package files. Hash
// staged bytes so replacement during an update cannot mix a digest and payload.
// Binary snapshots are removed after hashing and are never returned to the ZIP.
template<class FileHash>
inline Export exportPackageIdentity(const fs::path& hostRoot,const fs::path& destinationDirectory,FileHash hashFile){
    Export result{{{"schemaVersion",1},{"evidenceScope","export-time-disk-snapshots;not-loaded-image-proof"},
        {"sourceReadOnly",true},{"networkStarted",false},{"workerStarted",false},{"coherentAcrossFiles",false},
        {"components",Json::array()}},{}};
    static std::atomic<unsigned long long> sequence{0};
    bool rootReady=!hostRoot.empty()&&hostRoot.is_absolute();DWORD rootError=0;
    if(rootReady){const auto attributes=GetFileAttributesW(ymcc::logexport::winPath(hostRoot).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){rootError=GetLastError();rootReady=false;}
        else rootReady=(attributes&FILE_ATTRIBUTE_DIRECTORY)&&!(attributes&FILE_ATTRIBUTE_REPARSE_POINT);
    }
    result.receipt["rootObserved"]=rootReady;
    for(const auto* name:{L"CustomSteamLibrary.exe",L"SteamArtworkLab.exe",L"package-manifest.json"}){
        const bool manifest=std::wstring_view(name)==L"package-manifest.json";
        const auto limit=manifest?256ull*1024:64ull*1024*1024;
        Json item={{"name",utf8(name)},{"observed",false},{"maxBytes",limit}};
        if(!rootReady){item["reason"]="package-root-unavailable";if(rootError)item["win32Error"]=rootError;result.receipt["components"].push_back(item);continue;}
        const auto source=hostRoot/name;
        const auto attributes=GetFileAttributesW(ymcc::logexport::winPath(source).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){item["reason"]="package-file-unavailable";item["win32Error"]=GetLastError();result.receipt["components"].push_back(item);continue;}
        if(attributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT)){item["reason"]="package-file-not-regular";result.receipt["components"].push_back(item);continue;}
        std::error_code error;fs::create_directories(ymcc::logexport::winPath(destinationDirectory),error);
        if(error){item["reason"]="package-export-directory-failed";item["statusErrorCode"]=error.value();result.receipt["components"].push_back(item);continue;}
        const auto temporary=destinationDirectory/(L"custom-library-package.tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(sequence.fetch_add(1)));
        DWORD code=0;
        if(!ymcc::logexport::snapshotSharedLog(source,temporary,code,limit)){
            item["reason"]=code==ERROR_FILE_TOO_LARGE?"package-file-size-limit":"package-file-copy-failed";item["win32Error"]=code;result.receipt["components"].push_back(item);continue;
        }
        try{
            const auto bytes=fs::file_size(ymcc::logexport::winPath(temporary));
            const auto hash=hashFile(temporary.wstring());
            if(hash.size()!=64||!std::all_of(hash.begin(),hash.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}))throw std::runtime_error("package-digest-unavailable");
            item["bytes"]=bytes;item["sha256"]=hash;item["observed"]=true;item["reason"]="disk-snapshot";
            if(manifest){
                const auto destination=destinationDirectory/L"custom-library-package-manifest.json";
                if(MoveFileExW(ymcc::logexport::winPath(temporary).c_str(),ymcc::logexport::winPath(destination).c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)){
                    item["copied"]=true;result.files.push_back(destination);
                }else{item["copied"]=false;item["publishError"]=GetLastError();}
            }
        }catch(...){item["reason"]="package-digest-failed";}
        fs::remove(ymcc::logexport::winPath(temporary),error);
        if(error)item["temporaryCleanupError"]=error.value();
        result.receipt["components"].push_back(item);
    }
    return result;
}

} // namespace ymcc::media::diagnostics
