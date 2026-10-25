#pragma once
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>
#include <cstdint>
namespace ymcc::update_download {
struct CacheClearResult { uint64_t bytes=0; std::vector<std::string> errors; };
// Fixed leaf names only. Never recurse or consume page-supplied paths; install
// staging/rollback/journal and user policy are outside this operation.
inline CacheClearResult clearPackageCache(const std::filesystem::path& root) {
    CacheClearResult result;
    const auto attributes=GetFileAttributesW(root.c_str());
    if(attributes==INVALID_FILE_ATTRIBUTES) {
        const auto error=GetLastError();
        if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND) result.errors.push_back("Cannot inspect update cache directory");
        return result;
    }
    if(!(attributes&FILE_ATTRIBUTE_DIRECTORY) || (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
        result.errors.push_back("Update cache directory is a reparse point or not a directory"); return result;
    }
    HANDLE directory=CreateFileW(root.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE,
        nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
    if(directory==INVALID_HANDLE_VALUE) { result.errors.push_back("Cannot lock update cache directory"); return result; }
    struct DirectoryLease { HANDLE file; ~DirectoryLease(){CloseHandle(file);} } directoryLease{directory};
    BY_HANDLE_FILE_INFORMATION directoryInfo{};
    if(!GetFileInformationByHandle(directory,&directoryInfo) || !(directoryInfo.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) ||
        (directoryInfo.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)) {
        result.errors.push_back("Refusing changed or linked update cache directory"); return result;
    }
    for(const auto* name:{L"package.zip",L"package.zip.part",L"package.zip.part.json"}) {
        const auto path=root/name; const auto attr=GetFileAttributesW(path.c_str());
        if(attr==INVALID_FILE_ATTRIBUTES) {
            const auto error=GetLastError();
            if(error!=ERROR_FILE_NOT_FOUND && error!=ERROR_PATH_NOT_FOUND) result.errors.push_back("Cannot inspect cache entry");
            continue;
        }
        // File handles bind the checked object; deletion never follows a link.
        HANDLE file=CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT|FILE_FLAG_BACKUP_SEMANTICS,nullptr);
        if(file==INVALID_HANDLE_VALUE) { result.errors.push_back("Cache file is locked or access denied"); continue; }
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(file,&info) || (info.dwFileAttributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))) {
            result.errors.push_back("Refusing linked or non-file cache entry"); CloseHandle(file); continue;
        }
        FILE_DISPOSITION_INFO disposition{TRUE};
        if(SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof(disposition)))
            result.bytes+=(static_cast<uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
        else result.errors.push_back("Cannot delete cache file (locked, read-only or access denied)");
        CloseHandle(file);
    }
    return result;
}
}
