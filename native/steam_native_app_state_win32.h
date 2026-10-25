#pragma once
#include "steam_native_app_state_diagnostic.h"
#include "steam_native_record_win32.h"
#include <limits>

// Read a fixed-version native UI app object only inside the already captured
// original-game/account bracket. This provider never calls a Steam getter or
// admits an action. It does not establish the controller's login-generation lease.
namespace ymcc::steamappstatewin {
using Json = nlohmann::json;
using Context = ymcc::steamrecordwin::Context;
using HashFile = ymcc::steamrecordwin::HashFile;
using ymcc::steamrecordwin::Handle;
using ymcc::steamrecordwin::Module;
using ymcc::steamrecordwin::FileStamp;
using ymcc::steamrecordwin::pathKey;
using ymcc::steamrecordwin::exactProcess;
using ymcc::steamrecordwin::exactModule;
using ymcc::steamrecordwin::fileStamp;

inline Json unavailable(const char* reason) {
    return {{"source","steam-native-ui-app-state-diagnostic"},
        {"authority","diagnostic-only-no-admission"},{"status","unavailable"},
        {"available",false},{"reason",reason},{"nativeAppFlags",nullptr},
        {"runningBitSet",nullptr},{"atomicSnapshot",false},{"abaExcluded",false},
        {"nativeCapacityVerified",false},{"loginGenerationLease","unknown"},
        {"callbackControllerIdentityVerified",false},{"inputVerified",false},
        {"continueVerified",false},{"readCalls",0},{"readBytes",0}};
}
inline bool gameIdMatches(std::uint64_t gameId,std::uint32_t appid) {
    if(!gameId||!appid)return false;
    const auto kind=static_cast<std::uint32_t>((gameId>>24)&0xff);
    if(kind==2)return (gameId&0xffffff)==0&&static_cast<std::uint32_t>(gameId>>32)==appid;
    return kind==0&&(gameId>>32)==0&&static_cast<std::uint32_t>(gameId&0xffffff)==appid;
}
inline bool verifiedDiskProfile(const Context& c,const Module& m,const FileStamp& stamp,const HashFile& hash,ymcc::steamappstate::ProfileId* selected=nullptr,std::string* actualHash=nullptr,ymcc::steamcompat::Qualification* qualified=nullptr) {
    struct Cache {DWORD pid=0;std::uint64_t birth=0,base=0,size=0;std::wstring path;FileStamp stamp;bool accepted=false;std::string sha256;ymcc::steamcompat::Qualification compatibility;ymcc::steamappstate::ProfileId profile=ymcc::steamappstate::ProfileId::Legacy1791918634;};
    static std::mutex mx;static Cache cache;std::lock_guard<std::mutex> lock(mx);
    if(cache.pid==c.steamPid&&cache.birth==c.steamBirth&&cache.base==m.base&&cache.size==m.size&&cache.path==pathKey(m.path)&&cache.stamp==stamp){if(cache.accepted&&selected)*selected=cache.profile;if(actualHash)*actualHash=cache.sha256;if(qualified)*qualified=cache.compatibility;return cache.accepted;}
    // Retain the legacy callback argument for source compatibility. The
    // authoritative digest now shares the exact qualified file buffer.
    (void)hash;
    const auto compatibility=ymcc::steamcompat::qualifyFile(m.path,ymcc::steamcompat::ModuleKind::Ui);
    const auto& digest=compatibility.sha256;if(digest.size()!=64)return false;
    const auto profile=compatibility.layout ? std::optional<ymcc::steamappstate::ProfileId>(
        *compatibility.layout==ymcc::steamcompat::Layout::Public ? ymcc::steamappstate::ProfileId::Public1792618285 :
        ymcc::steamappstate::ProfileId::Legacy1791918634) : std::nullopt;
    FileStamp qualifiedStamp;if(!fileStamp(m.path,qualifiedStamp)||!(qualifiedStamp==stamp))return false;
    const bool accepted=profile.has_value();
    cache={c.steamPid,c.steamBirth,m.base,m.size,pathKey(m.path),stamp,accepted,digest,compatibility,
        profile.value_or(ymcc::steamappstate::ProfileId::Legacy1791918634)};
    if(accepted&&selected)*selected=*profile;
    if(actualHash)*actualHash=digest;
    if(qualified)*qualified=compatibility;
    return accepted;
}
inline bool readableRegion(HANDLE process,std::uint64_t address,std::size_t bytes) {
    if(!bytes||address<0x10000||address>0x7fffffffffffULL||bytes-1>0x7fffffffffffULL-address)return false;
    const auto end=address+bytes;
    // The pure reader uses small exact reads. Bound crossing of committed regions
    // as well; a pointer being mapped alone never proves that a map index is live.
    for(unsigned regions=0;address<end&&regions<4;++regions){
        MEMORY_BASIC_INFORMATION info{};
        if(!VirtualQueryEx(process,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),&info,sizeof(info))||info.State!=MEM_COMMIT||(info.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto protection=info.Protect&0xff;
        if(protection!=PAGE_READONLY&&protection!=PAGE_READWRITE&&protection!=PAGE_WRITECOPY&&protection!=PAGE_EXECUTE_READ&&protection!=PAGE_EXECUTE_READWRITE&&protection!=PAGE_EXECUTE_WRITECOPY)return false;
        const auto begin=static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(info.BaseAddress));
        if(begin>address||!info.RegionSize||info.RegionSize>(std::numeric_limits<std::uint64_t>::max)()-begin)return false;
        const auto next=begin+static_cast<std::uint64_t>(info.RegionSize);
        if(next<=address)return false;
        address=(std::min)(next,end);
    }
    return address==end;
}
inline Json observeImpl(const Context& c,std::uint32_t appid,const HashFile& hash,const std::function<bool()>& owned) {
    const auto began=GetTickCount64();
    if(!c.steamPid||!c.steamBirth||c.steamRoot.empty()||!c.gamePid||!c.gameBirth||c.gamePath.empty()||!c.account64||!c.sessionBound||!gameIdMatches(c.gameId64,appid))return unavailable("native-ui-original-binding-unavailable");
    if(!owned||!owned())return unavailable("native-ui-owner-changed");
    Handle steam(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,c.steamPid));
    Handle game(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,c.gamePid));
    const auto steamPath=c.steamRoot+L"\\steam.exe",modulePath=c.steamRoot+L"\\SteamUI.dll";
    if(!steam.valid()||!game.valid()||!exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession)||!exactProcess(game.h,c.gamePid,c.gameBirth,c.gamePath,c.windowsSession))return unavailable("native-ui-original-process-unavailable");
    Module m;FileStamp stamp;
    if(!exactModule(c.steamPid,modulePath,m)||!fileStamp(m.path,stamp))return unavailable("native-ui-module-unavailable");
    ymcc::steamappstate::ProfileId profile=ymcc::steamappstate::ProfileId::Legacy1791918634;
    std::string actualHash;ymcc::steamcompat::Qualification compatibility;
    if(!ymcc::steamappstatewin::verifiedDiskProfile(c,m,stamp,hash,&profile,&actualHash,&compatibility))return unavailable("native-ui-profile-unknown");
    const auto readingBegan=GetTickCount64();
    constexpr ULONGLONG maximumReadMs=100;
    const auto stable=[&]{
        if(GetTickCount64()-readingBegan>maximumReadMs||!owned()||!exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession)||!exactProcess(game.h,c.gamePid,c.gameBirth,c.gamePath,c.windowsSession))return false;
        Module now;FileStamp disk;
        return exactModule(c.steamPid,modulePath,now)&&now.base==m.base&&now.size==m.size&&pathKey(now.path)==pathKey(m.path)&&fileStamp(m.path,disk)&&disk==stamp;
    };
    const auto read=[&](std::uint64_t address,void* destination,std::size_t bytes){
        if(GetTickCount64()-readingBegan>maximumReadMs||!readableRegion(steam.h,address,bytes))return false;
        SIZE_T got=0;return ReadProcessMemory(steam.h,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),destination,bytes,&got)&&got==bytes;
    };
    const auto compatibleRead=ymcc::steamcompat::adaptProcessRead(compatibility,m.base,m.size,read);
    Json result;
    try{result=ymcc::steamappstate::observe({m.base,ymcc::steamcompat::referenceImageSize(compatibility,m.size),appid,true,profile},compatibleRead,stable);}
    catch(...){result=unavailable("native-ui-read-unavailable");}
    result["profile"]=ymcc::steamappstate::profileVersion(profile);
    result["diskSha256"]=actualHash;
    result["admission"]="verified-code-and-data-contract";result["wholeImageHashIsVersionGate"]=false;result["dynamicallyLocated"]=compatibility.dynamicallyLocated;
    result["loadedImageFullyHashed"]=false;
    result["profileVerificationElapsedMs"]=readingBegan-began;
    result["readElapsedMs"]=GetTickCount64()-readingBegan;
    result["totalElapsedMs"]=GetTickCount64()-began;
    result["maximumReadMs"]=maximumReadMs;
    result["gameProcessReadAccess"]=false;
    result["originalProcessBindingRequired"]=true;
    result["readTimeLimitIsCheckpointBased"]=true;
    result["loginGenerationLease"]="unknown";
    return result;
}
inline Json observe(const Context& c,std::uint32_t appid,const HashFile& hash,const std::function<bool()>& owned) {
    try{return observeImpl(c,appid,hash,owned);}catch(...){return unavailable("native-ui-read-unavailable");}
}
} // namespace ymcc::steamappstatewin
