#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "virtual_target_return_record.h"
#ifdef _WIN32
#include <windows.h>
#include <atomic>

// Explicit local file I/O, no retry, discovery, owner liveness or device APIs.
// The caller must serialize all owners and keep the containing directory stable.
// Atomic publication is NOT cross-process CAS, machine trust or recovery authority.
namespace ymcc::targetReturn {
enum class LoadState { Missing, Valid, RetryableIo, Invalid };
struct LoadResult {
    LoadState state=LoadState::Invalid;
    std::optional<Record> record;
    DWORD win32Error=0;
    std::string reason;
};
enum class StoreState { Committed, RetryableIo, Invalid };
struct StoreResult {
    StoreState state=StoreState::Invalid;
    DWORD win32Error=0;
    std::string reason;
    bool committed()const noexcept{return state==StoreState::Committed;}
};
namespace fileDetail {
inline constexpr std::size_t maxFilePathChars=4096;
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    explicit Handle(HANDLE h):value(h){}
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    Handle(const Handle&)=delete;Handle&operator=(const Handle&)=delete;
    bool valid()const noexcept{return value!=INVALID_HANDLE_VALUE;}
    bool close(){if(!valid())return true;if(!CloseHandle(value))return false;value=INVALID_HANDLE_VALUE;return true;}
};
inline bool normalize(const std::wstring&raw,std::wstring&path){
    if(raw.size()<4||raw.size()>maxFilePathChars||raw[1]!=L':'||(!(raw[0]>=L'A'&&raw[0]<=L'Z')&&!(raw[0]>=L'a'&&raw[0]<=L'z')))return false;
    path=raw;for(auto&c:path)if(c==L'/')c=L'\\';if(path[2]!=L'\\')return false;
    unsigned components=0;std::size_t start=3;
    for(std::size_t i=3;i<=path.size();++i){if(i==path.size()||path[i]==L'\\'){
        const auto s=path.substr(start,i-start);if(s.empty()||s==L"."||s==L".."||s.back()==L'.'||s.back()==L' '||++components>128)return false;
        std::wstring stem=s.substr(0,s.find(L'.'));for(auto&c:stem)if(c>=L'a'&&c<=L'z')c=wchar_t(c-L'a'+L'A');
        if(stem==L"CON"||stem==L"PRN"||stem==L"AUX"||stem==L"NUL"||(stem.size()==4&&(stem.substr(0,3)==L"COM"||stem.substr(0,3)==L"LPT")&&stem[3]>=L'0'&&stem[3]<=L'9'))return false;
        start=i+1;
    }else{const auto c=path[i];if(c<32||c==127||c==L':'||c==L'"'||c==L'<'||c==L'>'||c==L'|'||c==L'?'||c==L'*')return false;
        if(c>=0xd800&&c<=0xdbff){if(i+1>=path.size()||path[i+1]<0xdc00||path[i+1]>0xdfff)return false;++i;}else if(c>=0xdc00&&c<=0xdfff)return false;}}
    return true;
}
inline bool parents(const std::wstring&path,DWORD&error,bool&invalid){
    for(std::size_t i=2;i<path.size();++i)if(path[i]==L'\\'){
        const auto parent=path.substr(0,i==2?3:i);const DWORD a=GetFileAttributesW(parent.c_str());
        if(a==INVALID_FILE_ATTRIBUTES){error=GetLastError();return false;}
        if(!(a&FILE_ATTRIBUTE_DIRECTORY)||(a&FILE_ATTRIBUTE_REPARSE_POINT)){invalid=true;error=ERROR_INVALID_DATA;return false;}}
    return true;
}
inline bool info(HANDLE h,BY_HANDLE_FILE_INFORMATION&v,DWORD&error){
    if(GetFileType(h)!=FILE_TYPE_DISK){error=ERROR_INVALID_DATA;return false;}
    if(!GetFileInformationByHandle(h,&v)){error=GetLastError();return false;}
    if((v.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||v.nNumberOfLinks!=1){error=ERROR_INVALID_DATA;return false;}return true;
}
inline bool sameInfo(const BY_HANDLE_FILE_INFORMATION&a,const BY_HANDLE_FILE_INFORMATION&b){
    return a.dwVolumeSerialNumber==b.dwVolumeSerialNumber&&a.nFileIndexHigh==b.nFileIndexHigh&&a.nFileIndexLow==b.nFileIndexLow&&a.nFileSizeHigh==b.nFileSizeHigh&&a.nFileSizeLow==b.nFileSizeLow&&
        a.ftLastWriteTime.dwHighDateTime==b.ftLastWriteTime.dwHighDateTime&&a.ftLastWriteTime.dwLowDateTime==b.ftLastWriteTime.dwLowDateTime&&a.nNumberOfLinks==b.nNumberOfLinks&&a.dwFileAttributes==b.dwFileAttributes;
}
}
inline LoadResult load(const std::wstring&rawPath,const std::string&expectedMachineBinding){
    LoadResult r;std::wstring path;if(!fileDetail::normalize(rawPath,path)||!detail::text(expectedMachineBinding,4096)){r.reason="invalid-explicit-path-or-machine";return r;}
    DWORD e=0;bool invalid=false;if(!fileDetail::parents(path,e,invalid)){r.state=invalid?LoadState::Invalid:LoadState::RetryableIo;r.win32Error=e;r.reason="parent-directory-unavailable-or-unsafe";return r;}
    const DWORD leafAttributes=GetFileAttributesW(path.c_str());
    if(leafAttributes!=INVALID_FILE_ATTRIBUTES&&(leafAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){r.reason="record-file-kind-invalid";return r;}
    // SHARE_READ excludes in-place writers and replacement during this snapshot.
    fileDetail::Handle h(CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!h.valid()){r.win32Error=GetLastError();r.state=r.win32Error==ERROR_FILE_NOT_FOUND?LoadState::Missing:LoadState::RetryableIo;r.reason=r.state==LoadState::Missing?"record-missing":"record-open-failed";return r;}
    BY_HANDLE_FILE_INFORMATION before{},after{};if(!fileDetail::info(h.value,before,e)){r.win32Error=e;r.state=e==ERROR_INVALID_DATA?LoadState::Invalid:LoadState::RetryableIo;r.reason="record-file-kind-or-info-invalid";return r;}
    if(before.nFileSizeHigh||before.nFileSizeLow==0||before.nFileSizeLow>maxRecordBytes){r.reason="record-file-size-invalid";return r;}
    std::string bytes(before.nFileSizeLow,'\0');DWORD got=0;if(!ReadFile(h.value,bytes.data(),before.nFileSizeLow,&got,nullptr)||got!=before.nFileSizeLow){r.state=LoadState::RetryableIo;r.win32Error=GetLastError();r.reason="record-read-incomplete";return r;}
    char extra=0;DWORD extraRead=0;if(!ReadFile(h.value,&extra,1,&extraRead,nullptr)||extraRead||!fileDetail::info(h.value,after,e)||!fileDetail::sameInfo(before,after)){r.state=LoadState::RetryableIo;r.win32Error=e;r.reason="record-read-snapshot-changed";return r;}
    Record record;if(!decode(bytes,record,&r.reason)||record.machineBinding!=expectedMachineBinding){r.reason="record-invalid-or-foreign-machine";return r;}
    r.state=LoadState::Valid;r.record=std::move(record);r.reason="historical-provenance-only";return r;
}
inline StoreResult store(const std::wstring&rawPath,const Record&record){
    StoreResult r;std::string bytes;std::wstring path;if(!fileDetail::normalize(rawPath,path)||!encode(record,bytes,&r.reason)){r.reason="invalid-explicit-path-or-record";return r;}
    DWORD error=0;bool invalid=false;if(!fileDetail::parents(path,error,invalid)){r.state=invalid?StoreState::Invalid:StoreState::RetryableIo;r.win32Error=error;r.reason="parent-directory-unavailable-or-unsafe";return r;}
    const DWORD leafAttributes=GetFileAttributesW(path.c_str());
    if(leafAttributes!=INVALID_FILE_ATTRIBUTES&&(leafAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){r.reason="destination-file-kind-invalid";return r;}
    // Reject an unsafe destination without ever opening it for data writes.
    {fileDetail::Handle existing(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(existing.valid()){BY_HANDLE_FILE_INFORMATION info{};if(!fileDetail::info(existing.value,info,error)){r.win32Error=error;r.state=error==ERROR_INVALID_DATA?StoreState::Invalid:StoreState::RetryableIo;r.reason="destination-file-kind-invalid";return r;}}
        else{error=GetLastError();if(error!=ERROR_FILE_NOT_FOUND){r.state=StoreState::RetryableIo;r.win32Error=error;r.reason="destination-open-failed";return r;}}}
    static std::atomic<std::uint64_t> sequence{0};const auto suffix=sequence.fetch_add(1,std::memory_order_relaxed);
    const auto tmp=path+L".tmp."+std::to_wstring(GetCurrentProcessId())+L"."+std::to_wstring(GetTickCount64())+L"."+std::to_wstring(suffix);
    fileDetail::Handle h(CreateFileW(tmp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_WRITE_THROUGH|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
    if(!h.valid()){r.state=StoreState::RetryableIo;r.win32Error=GetLastError();r.reason="temporary-create-failed";return r;}
    auto fail=[&](const char*why,DWORD e){h.close();DeleteFileW(tmp.c_str());r.state=StoreState::RetryableIo;r.win32Error=e;r.reason=why;return r;};
    DWORD written=0;if(!WriteFile(h.value,bytes.data(),DWORD(bytes.size()),&written,nullptr)||written!=bytes.size())return fail("temporary-write-incomplete",GetLastError());
    if(!FlushFileBuffers(h.value))return fail("temporary-flush-failed",GetLastError());
    if(!h.close())return fail("temporary-close-failed",GetLastError());
    if(!MoveFileExW(tmp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))return fail("atomic-replace-failed",GetLastError());
    r.state=StoreState::Committed;r.reason="file-committed-not-ownership-cas";return r;
}
} // namespace ymcc::targetReturn
#endif
