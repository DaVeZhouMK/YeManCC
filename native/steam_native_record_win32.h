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
// The existing owned callback/ReadScope remains the caller's responsibility.
// This immutable observation records the copied query identity; it does not
// retain a login/power generation or authorize a current action on its own.
class TypedNativeRoleObservation final {
public:
    const Context& context() const noexcept { return context_; }
    const ymcc::steamrecord::TypedNativeRoleObservation& record() const noexcept { return record_; }
private:
    template<bool WithinBudget> friend Json observePolicy(const Context&, const HashFile&,
        const std::function<bool()>&, std::optional<TypedNativeRoleObservation>*);
    friend Json observeImpl(const Context&, const HashFile&, const std::function<bool()>&,
        std::optional<TypedNativeRoleObservation>*);
    TypedNativeRoleObservation(Context context, ymcc::steamrecord::TypedNativeRoleObservation record)
        : context_(std::move(context)), record_(std::move(record)) {}
    Context context_;
    ymcc::steamrecord::TypedNativeRoleObservation record_;
};
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
enum class ProfileReadPolicy { DefaultFull, CacheOnlyTryLock };
enum class ProfileReadStatus { Unknown, Ready, Busy, Cold };
inline bool verifiedDiskProfile(const Context& c,const Module& m,const FileStamp& stamp,const HashFile& hash,
    DiskProfile* selected=nullptr,ProfileReadPolicy policy=ProfileReadPolicy::DefaultFull,
    ProfileReadStatus* status=nullptr) {
    struct Cache {bool initialized=false;DWORD pid=0;std::uint64_t birth=0,base=0,size=0;
        std::wstring path;FileStamp stamp;DiskProfile profile;};
    static std::mutex mx;static Cache cache;
    if(selected)*selected={};
    if(status)*status=ProfileReadStatus::Unknown;
    std::unique_lock<std::mutex> lock(mx,std::defer_lock);
    if(policy==ProfileReadPolicy::CacheOnlyTryLock){
        if(!lock.try_lock()){if(status)*status=ProfileReadStatus::Busy;return false;}
    }else lock.lock();
    if(cache.initialized&&cache.pid==c.steamPid&&cache.birth==c.steamBirth&&cache.base==m.base&&cache.size==m.size&&cache.path==pathKey(m.path)&&cache.stamp==stamp){
        const bool accepted=cache.profile.profileId.has_value()&&
            ymcc::steamcompat::referenceBindingCurrent(cache.profile.compatibility);
        if(selected)*selected=cache.profile;
        if(status)*status=accepted?ProfileReadStatus::Ready:ProfileReadStatus::Unknown;
        return accepted;
    }
    if(policy==ProfileReadPolicy::CacheOnlyTryLock){if(status)*status=ProfileReadStatus::Cold;return false;}
    // Retain the legacy callback argument for source compatibility. The
    // authoritative digest now shares the exact qualified file buffer.
    (void)hash;
    const auto compatibility=ymcc::steamcompat::qualifyFile(m.path,ymcc::steamcompat::ModuleKind::Client);
    const auto& digest=compatibility.sha256;if(digest.size()!=64)return false;
    const auto profileId=compatibility.layout && ymcc::steamcompat::validLayout(*compatibility.layout) &&
        ymcc::steamcompat::referenceBindingCurrent(compatibility)
        ? std::optional<ymcc::steamrecord::ProfileId>(
            *compatibility.layout==ymcc::steamcompat::Layout::OwnerScoped ? ymcc::steamrecord::ProfileId::OwnerScoped :
            *compatibility.layout==ymcc::steamcompat::Layout::Public ? ymcc::steamrecord::ProfileId::Public1792618285 :
            ymcc::steamrecord::ProfileId::Legacy1791918634) : std::nullopt;
    FileStamp qualifiedStamp;if(!fileStamp(m.path,qualifiedStamp)||!(qualifiedStamp==stamp))return false;
    DiskProfile profile{digest,profileId,compatibility};
    // Cache the actual digest and structural result; unknown digests are allowed.
    // The same incompatible file is not rehashed
    // on every observation. Process/module/file identity changes invalidate it.
    cache={true,c.steamPid,c.steamBirth,m.base,m.size,pathKey(m.path),stamp,profile};
    if(selected)*selected=profile;
    if(status)*status=profile.profileId?ProfileReadStatus::Ready:ProfileReadStatus::Unknown;
    return profile.profileId.has_value();
}
// Module facts only: this is not a role read or an action/Ready grant.
enum class ProfilePreparationStatus {
    NotAttempted, Ready, BindingUnavailable, CurrentRejected, ProcessUnavailable,
    ModuleUnavailable, ProfileUnavailable, Changed, Exception
};
inline const char* profilePreparationLabel(ProfilePreparationStatus status) noexcept {
    switch(status){
    case ProfilePreparationStatus::NotAttempted:return "not-attempted";
    case ProfilePreparationStatus::Ready:return "module-profile-ready";
    case ProfilePreparationStatus::BindingUnavailable:return "binding-unavailable";
    case ProfilePreparationStatus::CurrentRejected:return "current-rejected";
    case ProfilePreparationStatus::ProcessUnavailable:return "steam-process-unavailable";
    case ProfilePreparationStatus::ModuleUnavailable:return "module-unavailable";
    case ProfilePreparationStatus::ProfileUnavailable:return "profile-unavailable";
    case ProfilePreparationStatus::Changed:return "module-or-process-changed";
    case ProfilePreparationStatus::Exception:return "preparation-exception";
    }
    return "preparation-exception";
}
inline ProfilePreparationStatus prepareModuleProfile(const Context& supplied,
    const std::function<bool()>& current) noexcept {
    try {
        if(!current||!current())return ProfilePreparationStatus::CurrentRejected;
        const Context c=supplied;
        if(!c.steamPid||!c.steamBirth||c.steamRoot.empty()||!c.sessionBound)
            return ProfilePreparationStatus::BindingUnavailable;
        Handle steam(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,c.steamPid));
        const auto steamPath=c.steamRoot+L"\\steam.exe",modulePath=c.steamRoot+L"\\steamclient64.dll";
        if(!steam.valid()||!exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession))
            return ProfilePreparationStatus::ProcessUnavailable;
        if(!current())return ProfilePreparationStatus::CurrentRejected;
        Module module;FileStamp stamp;
        if(!exactModule(c.steamPid,modulePath,module)||!fileStamp(module.path,stamp))
            return ProfilePreparationStatus::ModuleUnavailable;
        if(!current())return ProfilePreparationStatus::CurrentRejected;
        // Keep the original process-local cache/lock and qualifier. DefaultFull
        // is not interruptibly cancellable; it may commit old-key module facts
        // after revocation. Only the outer result/business adoption is refused.
        const bool available=verifiedDiskProfile(c,module,stamp,HashFile{},nullptr,ProfileReadPolicy::DefaultFull);
        if(!current())return ProfilePreparationStatus::CurrentRejected;
        Module after;FileStamp afterStamp;
        if(!exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession)||
            !exactModule(c.steamPid,modulePath,after)||after.base!=module.base||after.size!=module.size||
            pathKey(after.path)!=pathKey(module.path)||!fileStamp(after.path,afterStamp)||!(afterStamp==stamp))
            return ProfilePreparationStatus::Changed;
        if(!current())return ProfilePreparationStatus::CurrentRejected;
        return available?ProfilePreparationStatus::Ready:ProfilePreparationStatus::ProfileUnavailable;
    }catch(...){return ProfilePreparationStatus::Exception;}
}
inline const char* profileLabel(ymcc::steamrecord::ProfileId id) {
    switch(id){
    case ymcc::steamrecord::ProfileId::Legacy1791918634:return "win64-client1791918634-e9045710";
    case ymcc::steamrecord::ProfileId::Public1792618285:return "win64-client1792618285-253e0a11";
    case ymcc::steamrecord::ProfileId::OwnerScoped:return "win64-client-owner-scoped-ebf46f84";
    }
    return nullptr;
}
inline Json unavailable(const char* reason) {
    return {{"source","steam-native-cuser-record-diagnostic"},{"authority","diagnostic-only-no-admission"},
        {"status","unavailable"},{"reason",reason},{"recordState","unknown"},{"atomicSnapshot",false},
        {"loginGenerationLease","unknown"},{"recordAbsenceIsNotAtomicProof",true},{"inputVerified",false},{"continueVerified",false},
        {"accountValuesPersisted",false},{"otherAccountsRecordsRead",false},{"readCalls",0},{"readBytes",0}};
}
template<bool WithinBudget> inline Json observePolicy(const Context& supplied,const HashFile& hash,const std::function<bool()>& owned,
    std::optional<TypedNativeRoleObservation>* typedOutput) {
    const Context c=supplied;
    if(typedOutput)typedOutput->reset();
    const auto began=GetTickCount64();
    if(!c.steamPid||!c.steamBirth||c.steamRoot.empty()||!c.gamePid||!c.gameBirth||c.gamePath.empty()||!c.account64||!c.gameId64||!c.sessionBound)return unavailable("native-record-original-binding-unavailable");
    if(!owned||!owned())return unavailable("native-record-owner-changed");
    Handle steam(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,c.steamPid));
    Handle heldGame(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,c.gamePid));
    const auto steamPath=c.steamRoot+L"\\steam.exe",modulePath=c.steamRoot+L"\\steamclient64.dll";
    if(!steam.valid()||!heldGame.valid()||!exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession)||!exactProcess(heldGame.h,c.gamePid,c.gameBirth,c.gamePath,c.windowsSession))return unavailable("native-record-original-process-unavailable");
    Module m;FileStamp stamp;
    if(!exactModule(c.steamPid,modulePath,m)||!fileStamp(m.path,stamp))return unavailable("native-record-module-unavailable");
    DiskProfile diskProfile;
    ProfileReadStatus profileStatus=ProfileReadStatus::Unknown;
    if(!verifiedDiskProfile(c,m,stamp,hash,&diskProfile,
        WithinBudget?ProfileReadPolicy::CacheOnlyTryLock:ProfileReadPolicy::DefaultFull,&profileStatus))
        return unavailable(profileStatus==ProfileReadStatus::Busy?"native-record-profile-busy":
            profileStatus==ProfileReadStatus::Cold?"native-record-profile-cold":"native-record-profile-unknown");
    const auto stable=[&]{
        if(!owned()||!exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession)||!exactProcess(heldGame.h,c.gamePid,c.gameBirth,c.gamePath,c.windowsSession))return false;
        Module now;FileStamp disk;
        return exactModule(c.steamPid,modulePath,now)&&now.base==m.base&&now.size==m.size&&pathKey(now.path)==pathKey(m.path)&&fileStamp(m.path,disk)&&disk==stamp;
    };
    const auto read=[&](std::uint64_t address,void* dest,std::size_t size){
        if constexpr(WithinBudget){if(!owned())return false;}
        SIZE_T got=0;return ReadProcessMemory(steam.h,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),dest,size,&got)&&got==size;
    };
    const auto readViews=ymcc::steamcompat::makeProcessReadViews(diskProfile.compatibility,m.base,m.size,read);
    Json result;
    std::optional<ymcc::steamrecord::TypedNativeRoleObservation> record;
    try {result=ymcc::steamrecord::observeWithReadViews({m.base,ymcc::steamcompat::referenceImageSize(diskProfile.compatibility,m.size),c.account64,c.gameId64,c.gamePid,true,*diskProfile.profileId},readViews,stable,typedOutput?&record:nullptr);}
    catch(...){result=unavailable("native-record-read-unavailable");}
    result["profile"]=profileLabel(*diskProfile.profileId);
    result["diskSha256"]=diskProfile.sha256;
    result["admission"]= "verified-code-and-data-contract";result["wholeImageHashIsVersionGate"]=false;result["dynamicallyLocated"]=diskProfile.compatibility.dynamicallyLocated;
    result["loadedImageFullyHashed"]=false; // disk hash + loaded anchors, not a relocated-image hash
    result["readElapsedMs"]=GetTickCount64()-began;
    result["gameProcessReadAccess"]=false;
    if constexpr(WithinBudget){if(!owned())return unavailable("native-record-owner-changed");}
    if(typedOutput&&record)*typedOutput=TypedNativeRoleObservation(c,std::move(*record));
    return result;
}
inline Json observeImpl(const Context& c,const HashFile& hash,const std::function<bool()>& owned,
    std::optional<TypedNativeRoleObservation>* typedOutput=nullptr) {
    return observePolicy<false>(c,hash,owned,typedOutput);
}
inline Json observeWithinBudget(const Context& c,const HashFile& hash,const std::function<bool()>& admitted,
    std::optional<TypedNativeRoleObservation>* typedOutput) {
    try{return observePolicy<true>(c,hash,admitted,typedOutput);}
    catch(...){if(typedOutput)typedOutput->reset();return unavailable("native-record-read-unavailable");}
}
inline Json observeWithRoles(const Context& c,const HashFile& hash,const std::function<bool()>& owned,
    std::optional<TypedNativeRoleObservation>* typedOutput) {
    try{return observeImpl(c,hash,owned,typedOutput);}
    catch(...){if(typedOutput)typedOutput->reset();return unavailable("native-record-read-unavailable");}
}
inline Json observe(const Context& c,const HashFile& hash,const std::function<bool()>& owned) {
    return observeWithRoles(c,hash,owned,nullptr);
}
} // namespace ymcc::steamrecordwin
