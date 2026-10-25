#pragma once
#include "steam_native_record_diagnostic.h"
#include "steam_compatibility_win32.h"
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <cwctype>
#include <cctype>
#include <iterator>
#include <functional>
#include <mutex>
#include <string>

// This provider only reads the already-bound Steam process. Its result must
// never admit a recovery action, choose a game/account, or replace a tracker.
namespace ymcc::steamrecordwin {
using Json = nlohmann::json;
struct Context {
    DWORD steamPid=0,gamePid=0,windowsSession=0;
    std::uint64_t steamBirth=0,gameBirth=0,account64=0,gameId64=0;
    std::wstring steamRoot,gamePath;
    bool sessionBound=false;
};
using HashFile=std::function<std::string(const std::wstring&)>;
inline std::wstring pathKey(std::wstring s) {
    std::replace(s.begin(),s.end(),L'/',L'\\');
    std::transform(s.begin(),s.end(),s.begin(),[](wchar_t c){return static_cast<wchar_t>(std::towlower(c));});
    while(!s.empty()&&s.back()==L'\\')s.pop_back();return s;
}
struct Handle {
    HANDLE h=nullptr;
    explicit Handle(HANDLE value=nullptr):h(value){}
    ~Handle(){if(h&&h!=INVALID_HANDLE_VALUE)CloseHandle(h);}
    Handle(const Handle&)=delete;Handle& operator=(const Handle&)=delete;
    bool valid()const{return h&&h!=INVALID_HANDLE_VALUE;}
};
inline bool exactProcess(HANDLE h,DWORD pid,std::uint64_t birth,const std::wstring& expected,DWORD session) {
    if(!h||h==INVALID_HANDLE_VALUE||!pid||!birth||expected.empty()||WaitForSingleObject(h,0)!=WAIT_TIMEOUT)return false;
    FILETIME c{},e{},k{},u{};if(!GetProcessTimes(h,&c,&e,&k,&u))return false;
    if((std::uint64_t(c.dwHighDateTime)<<32|c.dwLowDateTime)!=birth)return false;
    wchar_t path[32768]{};DWORD n=static_cast<DWORD>(std::size(path)),observed=0;
    return QueryFullProcessImageNameW(h,0,path,&n)&&ProcessIdToSessionId(pid,&observed)&&observed==session&&pathKey(std::wstring(path,n))==pathKey(expected);
}
struct Module {std::uint64_t base=0,size=0;std::wstring path;};
inline bool exactModule(DWORD pid,const std::wstring& expected,Module& out) {
    Handle snap(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));if(!snap.valid())return false;
    MODULEENTRY32W row{};row.dwSize=sizeof(row);unsigned matches=0;
    if(Module32FirstW(snap.h,&row))do {
        if(pathKey(row.szExePath)==pathKey(expected)){
            ++matches;out.base=reinterpret_cast<std::uintptr_t>(row.modBaseAddr);out.size=row.modBaseSize;out.path=row.szExePath;
        }
    }while(Module32NextW(snap.h,&row));
    return matches==1&&out.base>=0x10000&&out.size!=0;
}
struct FileStamp {
    DWORD volume=0,indexHi=0,indexLo=0,sizeHi=0,sizeLo=0,timeHi=0,timeLo=0;
    bool operator==(const FileStamp&)const=default;
};
inline bool fileStamp(const std::wstring& path,FileStamp& out) {
    Handle file(CreateFileW(path.c_str(),FILE_READ_ATTRIBUTES,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr));
    BY_HANDLE_FILE_INFORMATION i{};if(!file.valid()||!GetFileInformationByHandle(file.h,&i)||(i.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY))return false;
    out={i.dwVolumeSerialNumber,i.nFileIndexHigh,i.nFileIndexLow,i.nFileSizeHigh,i.nFileSizeLow,i.ftLastWriteTime.dwHighDateTime,i.ftLastWriteTime.dwLowDateTime};return true;
}
struct DiskProfile {
    std::string sha256;
    std::optional<ymcc::steamrecord::ProfileId> profileId;
    ymcc::steamcompat::Qualification compatibility;
};
inline bool verifiedDiskProfile(const Context& c,const Module& m,const FileStamp& stamp,const HashFile& hash,
    DiskProfile* selected=nullptr) {
    struct Cache {bool initialized=false;DWORD pid=0;std::uint64_t birth=0,base=0,size=0;
        std::wstring path;FileStamp stamp;DiskProfile profile;};
    static std::mutex mx;static Cache cache;std::lock_guard<std::mutex> lock(mx);
    if(selected)*selected={};
    if(cache.initialized&&cache.pid==c.steamPid&&cache.birth==c.steamBirth&&cache.base==m.base&&cache.size==m.size&&cache.path==pathKey(m.path)&&cache.stamp==stamp){
        if(selected)*selected=cache.profile;
        return cache.profile.profileId.has_value();
    }
    // Retain the legacy callback argument for source compatibility. The
    // authoritative digest now shares the exact qualified file buffer.
    (void)hash;
    const auto compatibility=ymcc::steamcompat::qualifyFile(m.path,ymcc::steamcompat::ModuleKind::Client);
    const auto& digest=compatibility.sha256;if(digest.size()!=64)return false;
    const auto profileId=compatibility.layout ? std::optional<ymcc::steamrecord::ProfileId>(
        *compatibility.layout==ymcc::steamcompat::Layout::Public ? ymcc::steamrecord::ProfileId::Public1792618285 :
        ymcc::steamrecord::ProfileId::Legacy1791918634) : std::nullopt;
    FileStamp qualifiedStamp;if(!fileStamp(m.path,qualifiedStamp)||!(qualifiedStamp==stamp))return false;
    DiskProfile profile{digest,profileId,compatibility};
    // Cache the actual digest and structural result; unknown digests are allowed.
    // The same incompatible file is not rehashed
    // on every observation. Process/module/file identity changes invalidate it.
    cache={true,c.steamPid,c.steamBirth,m.base,m.size,pathKey(m.path),stamp,profile};
    if(selected)*selected=profile;
    return profile.profileId.has_value();
}
inline const char* profileLabel(ymcc::steamrecord::ProfileId id) {
    switch(id){
    case ymcc::steamrecord::ProfileId::Legacy1791918634:return "win64-client1791918634-e9045710";
    case ymcc::steamrecord::ProfileId::Public1792618285:return "win64-client1792618285-253e0a11";
    }
    return nullptr;
}
inline Json unavailable(const char* reason) {
    return {{"source","steam-native-cuser-record-diagnostic"},{"authority","diagnostic-only-no-admission"},
        {"status","unavailable"},{"reason",reason},{"recordState","unknown"},{"atomicSnapshot",false},
        {"loginGenerationLease","unknown"},{"recordAbsenceIsNotAtomicProof",true},{"inputVerified",false},{"continueVerified",false},
        {"accountValuesPersisted",false},{"otherAccountsRecordsRead",false},{"readCalls",0},{"readBytes",0}};
}
inline Json observeImpl(const Context& c,const HashFile& hash,const std::function<bool()>& owned) {
    const auto began=GetTickCount64();
    if(!c.steamPid||!c.steamBirth||c.steamRoot.empty()||!c.gamePid||!c.gameBirth||c.gamePath.empty()||!c.account64||!c.gameId64||!c.sessionBound)return unavailable("native-record-original-binding-unavailable");
    if(!owned||!owned())return unavailable("native-record-owner-changed");
    Handle steam(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,c.steamPid));
    Handle game(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,c.gamePid));
    const auto steamPath=c.steamRoot+L"\\steam.exe",modulePath=c.steamRoot+L"\\steamclient64.dll";
    if(!steam.valid()||!game.valid()||!exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession)||!exactProcess(game.h,c.gamePid,c.gameBirth,c.gamePath,c.windowsSession))return unavailable("native-record-original-process-unavailable");
    Module m;FileStamp stamp;
    if(!exactModule(c.steamPid,modulePath,m)||!fileStamp(m.path,stamp))return unavailable("native-record-module-unavailable");
    DiskProfile diskProfile;
    if(!verifiedDiskProfile(c,m,stamp,hash,&diskProfile))return unavailable("native-record-profile-unknown");
    const auto stable=[&]{
        if(!owned()||!exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession)||!exactProcess(game.h,c.gamePid,c.gameBirth,c.gamePath,c.windowsSession))return false;
        Module now;FileStamp disk;
        return exactModule(c.steamPid,modulePath,now)&&now.base==m.base&&now.size==m.size&&pathKey(now.path)==pathKey(m.path)&&fileStamp(m.path,disk)&&disk==stamp;
    };
    const auto read=[&](std::uint64_t address,void* dest,std::size_t size){
        SIZE_T got=0;return ReadProcessMemory(steam.h,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),dest,size,&got)&&got==size;
    };
    const auto compatibleRead=ymcc::steamcompat::adaptProcessRead(diskProfile.compatibility,m.base,m.size,read);
    Json result;
    try {result=ymcc::steamrecord::observe({m.base,ymcc::steamcompat::referenceImageSize(diskProfile.compatibility,m.size),c.account64,c.gameId64,c.gamePid,true,*diskProfile.profileId},compatibleRead,stable);}
    catch(...){result=unavailable("native-record-read-unavailable");}
    result["profile"]=profileLabel(*diskProfile.profileId);
    result["diskSha256"]=diskProfile.sha256;
    result["admission"]= "verified-code-and-data-contract";result["wholeImageHashIsVersionGate"]=false;result["dynamicallyLocated"]=diskProfile.compatibility.dynamicallyLocated;
    result["loadedImageFullyHashed"]=false; // disk hash + loaded anchors, not a relocated-image hash
    result["readElapsedMs"]=GetTickCount64()-began;
    result["gameProcessReadAccess"]=false;
    return result;
}
inline Json observe(const Context& c,const HashFile& hash,const std::function<bool()>& owned) {
    try{return observeImpl(c,hash,owned);}catch(...){return unavailable("native-record-read-unavailable");}
}
} // namespace ymcc::steamrecordwin
