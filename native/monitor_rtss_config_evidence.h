#pragma once
#include <windows.h>
#include <json.hpp>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>

// Explicit log export only. No discovery, SDK call, writer, cache or timer.
// Binary configuration is evidence, never a JSON text or control input.
namespace ymcc::rtssconfigevidence {
using Json = nlohmann::json;
inline constexpr std::uint64_t maxFileBytes = 1024ULL * 1024;
inline std::string base64(const std::string& bytes) {
    static constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out; out.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t i=0; i<bytes.size(); i+=3) {
        const auto a=static_cast<unsigned char>(bytes[i]);
        const bool second=i+1<bytes.size(), third=i+2<bytes.size();
        const auto b=second?static_cast<unsigned char>(bytes[i+1]):0;
        const auto c=third?static_cast<unsigned char>(bytes[i+2]):0;
        out.push_back(alphabet[a>>2]); out.push_back(alphabet[((a&3)<<4)|(b>>4)]);
        out.push_back(second?alphabet[((b&15)<<2)|(c>>6)]:'=');
        out.push_back(third?alphabet[c&63]:'=');
    }
    return out;
}
struct FileHandle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~FileHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    FileHandle(const FileHandle&)=delete;
    FileHandle& operator=(const FileHandle&)=delete;
    explicit FileHandle(HANDLE handle):value(handle){}
};
inline std::uint64_t sizeOf(const BY_HANDLE_FILE_INFORMATION& info) {
    return (std::uint64_t(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
}
inline Json captureFile(const std::filesystem::path& path,const char* entry) {
    Json row={{"entry",entry},{"maxBytes",maxFileBytes},{"capturedBytes",0},
        {"captureComplete",false},{"metadataStable",false},{"encoding","base64-original-bytes"}};
    FileHandle file(CreateFileW(path.c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN|FILE_FLAG_BACKUP_SEMANTICS,nullptr));
    if(file.value==INVALID_HANDLE_VALUE){
        const DWORD error=GetLastError();
        row["status"]=error==ERROR_FILE_NOT_FOUND||error==ERROR_PATH_NOT_FOUND?"missing":"open-error";
        row["win32Error"]=error;return row;
    }
    BY_HANDLE_FILE_INFORMATION before{};
    if(GetFileType(file.value)!=FILE_TYPE_DISK){row["status"]="not-regular-file";return row;}
    if(!GetFileInformationByHandle(file.value,&before)){row["status"]="metadata-error";row["win32Error"]=GetLastError();return row;}
    if(before.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY){row["status"]="not-regular-file";return row;}
    const auto expected=sizeOf(before);row["sourceBytesBefore"]=expected;
    if(expected>maxFileBytes){row["status"]="over-limit";return row;}
    std::string bytes(static_cast<std::size_t>(expected),'\0');
    std::size_t offset=0;DWORD error=ERROR_SUCCESS;
    while(offset<bytes.size()){
        DWORD read=0;const DWORD wanted=static_cast<DWORD>((std::min)(bytes.size()-offset,std::size_t(65536)));
        if(!ReadFile(file.value,bytes.data()+offset,wanted,&read,nullptr)){error=GetLastError();if(error==ERROR_SUCCESS)error=ERROR_READ_FAULT;break;}
        if(!read)break;
        offset+=read;
    }
    bytes.resize(offset);row["capturedBytes"]=offset;row["bytesBase64"]=base64(bytes);
    BY_HANDLE_FILE_INFORMATION after{};const bool metadata=GetFileInformationByHandle(file.value,&after)!=FALSE;
    const bool complete=offset==expected&&error==ERROR_SUCCESS;
    const bool stable=metadata&&before.dwVolumeSerialNumber==after.dwVolumeSerialNumber&&
        before.nFileIndexHigh==after.nFileIndexHigh&&before.nFileIndexLow==after.nFileIndexLow&&
        expected==sizeOf(after)&&CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0;
    row["captureComplete"]=complete;row["metadataStable"]=stable;
    if(metadata)row["sourceBytesAfter"]=sizeOf(after);
    row["status"]=error!=ERROR_SUCCESS?"read-error":!complete?"partial-read":!metadata?"metadata-unavailable":!stable?"changed-during-read":"captured";
    if(error!=ERROR_SUCCESS)row["win32Error"]=error;
    return row;
}
inline Json capture(const std::wstring& registeredGlobalPath,std::uint64_t observedAtMs) noexcept {
    try {
        Json result={{"schemaVersion",1},{"observedAtMonotonicMs",observedAtMs},
            {"evidenceScope","export-time-file-handle-bytes;not-live-RTSS-readback-or-atomic-session"},
            {"pathSource","existing-validated-directory-cache"},{"discoveryPerformed",false},
            {"configurationWrites",false},{"sdkCalls",false},{"activeProcessMatchChecked",false},
            {"maxBytesPerFile",maxFileBytes},{"maxTotalInputBytes",2*maxFileBytes},{"files",Json::array()}};
        if(registeredGlobalPath.empty()){result["status"]="directory-unregistered";return result;}
        const std::filesystem::path global(registeredGlobalPath);
        if(!global.is_absolute()||global.filename()!=L"Global"||global.parent_path().filename()!=L"Profiles"){
            result["status"]="invalid-registered-path";return result;
        }
        const auto root=global.parent_path().parent_path();result["status"]="observed";
        result["files"].push_back(captureFile(root/L"Plugins"/L"Client"/L"OverlayEditor.cfg","Plugins/Client/OverlayEditor.cfg"));
        result["files"].push_back(captureFile(global,"Profiles/Global"));
        return result;
    }catch(...){return Json();} // No diagnostic exception may escape.
}
} // namespace ymcc::rtssconfigevidence
