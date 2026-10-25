#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "steam_shortcut_metadata_diagnostic.h"
#include "steam_native_record_win32.h"
namespace ymcc::shortcutmetadatawin {
namespace old=ymcc::steamrecordwin;
using Outcome=ymcc::shortcutmetadata::Outcome;
using Phase=ymcc::shortcutmetadata::Phase;
using Witness=ymcc::shortcutmetadata::Witness;
struct Request {
    old::Context original;
    std::uint32_t appid=0;
    std::uint64_t generation=0,epoch=0;
    Phase phase=Phase::FollowOriginal;
    Witness witness;
    // This is an internal owner-gate assertion, never a public/CLI permission.
    // Baseline publication still needs the existing beforeCapture timestamp fence.
    bool beforeCaptureAdmitted=false;
};
inline Outcome unavailable(const char* reason){Outcome out{ymcc::shortcutmetadata::detail::empty(),{}};out.diagnostic["reason"]=reason;return out;}
inline bool closeChecked(old::Handle& h){if(!h.valid())return true;const auto value=h.h;h.h=nullptr;return CloseHandle(value)!=0;}
inline bool exactBoundedModule(DWORD pid,const std::wstring& expected,old::Module& out){
 old::Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE|TH32CS_SNAPMODULE32,pid));if(!snapshot.valid())return false;
 MODULEENTRY32W row{};row.dwSize=sizeof(row);bool ok=Module32FirstW(snapshot.h,&row)!=0;unsigned count=0,matches=0;bool accepted=ok;
 while(ok){
  if(++count>512){accepted=false;break;}
  if(old::pathKey(row.szModule)==L"steamclient64.dll"){
   if(old::pathKey(row.szExePath)!=old::pathKey(expected)){accepted=false;break;}
   ++matches;out.base=reinterpret_cast<std::uintptr_t>(row.modBaseAddr);out.size=row.modBaseSize;out.path=row.szExePath;
  }
  ok=Module32NextW(snapshot.h,&row)!=0;
 }
 if(accepted&&GetLastError()!=ERROR_NO_MORE_FILES)accepted=false;
 return closeChecked(snapshot)&&accepted&&matches==1&&out.base>=0x10000&&out.size!=0;
}
inline std::string utf8Key(const std::wstring& path){
 const auto value=old::pathKey(path);if(value.empty())return {};
 const auto n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),nullptr,0,nullptr,nullptr);if(n<=0)return {};
 std::string out(static_cast<std::size_t>(n),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value.data(),static_cast<int>(value.size()),out.data(),n,nullptr,nullptr)!=n)return {};return out;
}
inline bool readable(HANDLE process,std::uint64_t address,std::size_t size,const old::Module& module){
 MEMORY_BASIC_INFORMATION q{};if(VirtualQueryEx(process,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),&q,sizeof(q))!=sizeof(q))return false;
 const auto base=reinterpret_cast<std::uintptr_t>(q.BaseAddress);if(q.State!=MEM_COMMIT||q.Protect&PAGE_GUARD||address<base||address-base>q.RegionSize||size>q.RegionSize-(address-base))return false;
 switch(q.Protect&0xff){case PAGE_READONLY:case PAGE_READWRITE:case PAGE_WRITECOPY:case PAGE_EXECUTE_READ:case PAGE_EXECUTE_READWRITE:case PAGE_EXECUTE_WRITECOPY:break;default:return false;}
 if(module.base<=address&&address-module.base<module.size)return size<=module.size-(address-module.base)&&q.Type==MEM_IMAGE&&reinterpret_cast<std::uintptr_t>(q.AllocationBase)==module.base;
 return q.Type==MEM_PRIVATE||q.Type==MEM_MAPPED||q.Type==MEM_IMAGE;
}
inline Outcome observeImpl(const Request& input,const old::HashFile& hash,const std::function<bool()>& owned){
 const Request request=input;const auto& c=request.original;
 if(!c.steamPid||!c.steamBirth||!c.gamePid||!c.gameBirth||c.steamRoot.empty()||c.gamePath.empty()||!c.account64||!c.gameId64||!c.sessionBound||!request.generation||!request.epoch)return unavailable("original-binding-unavailable");
 if(!owned||!owned())return unavailable("original-owner-changed");
 if(request.phase==Phase::Baseline&&!request.beforeCaptureAdmitted)return unavailable("baseline-before-capture-unadmitted");
 if(request.phase==Phase::FollowOriginal&&!request.witness.valid)return unavailable("original-witness-missing");
 const auto steamPath=c.steamRoot+L"\\steam.exe",modulePath=c.steamRoot+L"\\steamclient64.dll";
 ymcc::shortcutmetadata::Scope scope{c.steamPid,c.gamePid,c.windowsSession,c.steamBirth,c.gameBirth,request.generation,request.epoch,utf8Key(steamPath),utf8Key(c.gamePath)};
 if(request.phase==Phase::FollowOriginal&&(!(scope==request.witness.scope)||request.witness.account64!=c.account64||request.witness.gameId64!=c.gameId64||request.witness.appid!=request.appid))return unavailable("original-witness-mismatch");
 old::Handle steam(OpenProcess(PROCESS_QUERY_INFORMATION|PROCESS_VM_READ|SYNCHRONIZE,FALSE,c.steamPid));
 old::Handle game(OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION|SYNCHRONIZE,FALSE,c.gamePid));
 if(!steam.valid()||!game.valid()||!old::exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession)||!old::exactProcess(game.h,c.gamePid,c.gameBirth,c.gamePath,c.windowsSession))return unavailable("original-process-unavailable");
 old::Module module;old::FileStamp stamp;
 if(!exactBoundedModule(c.steamPid,modulePath,module)||!old::fileStamp(module.path,stamp))return unavailable("original-module-unavailable");
 old::DiskProfile diskProfile;
 if(!old::verifiedDiskProfile(c,module,stamp,hash,&diskProfile)||
     (diskProfile.profileId!=ymcc::steamrecord::ProfileId::Legacy1791918634||
      diskProfile.sha256!=ymcc::steamrecord::kClientSha256))
  return unavailable("original-module-profile-unknown");
 const auto stable=[&]{
  if(!owned()||!old::exactProcess(steam.h,c.steamPid,c.steamBirth,steamPath,c.windowsSession)||!old::exactProcess(game.h,c.gamePid,c.gameBirth,c.gamePath,c.windowsSession))return false;
  old::Module now;old::FileStamp disk;return exactBoundedModule(c.steamPid,modulePath,now)&&now.base==module.base&&now.size==module.size&&old::pathKey(now.path)==old::pathKey(module.path)&&old::fileStamp(module.path,disk)&&disk==stamp;
 };
 const auto read=[&](std::uint64_t a,void* p,std::size_t n){
  if(!readable(steam.h,a,n,module))return false;SIZE_T got=0;
  return ReadProcessMemory(steam.h,reinterpret_cast<const void*>(static_cast<std::uintptr_t>(a)),p,n,&got)&&got==n;
 };
 ymcc::shortcutmetadata::Query query;query.native={module.base,module.size,c.account64,c.gameId64,c.gamePid,true};query.appid=request.appid;query.scope=scope;query.phase=request.phase;query.original=request.witness;query.originalTrackerBound=true;
 const auto began=GetTickCount64();auto result=ymcc::shortcutmetadata::observe(query,read,stable,[]{return GetTickCount64();});
 result.diagnostic["adapterElapsedMs"]=GetTickCount64()-began;
 if(!stable())result=unavailable("original-owner-changed");
 const bool gameClosed=closeChecked(game),steamClosed=closeChecked(steam);if(!gameClosed||!steamClosed)return unavailable("observation-handle-close-failed");
 result.diagnostic["profile"]="fixed-client1791918634-shortcut-metadata";result.diagnostic["diskSha256"]=ymcc::steamrecord::kClientSha256;
 result.diagnostic["loadedImageFullyHashed"]=false;result.diagnostic["gameProcessReadAccess"]=false;result.diagnostic["steamWrites"]=0;result.diagnostic["privateCalls"]=0;
 return result;
}
inline Outcome observe(const Request& request,const old::HashFile& hash,const std::function<bool()>& owned){try{return observeImpl(request,hash,owned);}catch(...){return unavailable("metadata-adapter-unavailable");}}
}

